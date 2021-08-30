#!/usr/bin/env python2

# Copyright 2013-present Barefoot Networks, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import signal
import os
import sys
import subprocess
import argparse
import json
import importlib
import re
from time import sleep

from mininet.net import Mininet
from mininet.topo import Topo
from mininet.link import TCLink
from mininet.log import setLogLevel, info
from mininet.cli import CLI
from mininet.node import ( Node, Host, OVSKernelSwitch, DefaultController, RemoteController, Controller )
from mininet.link import (Intf, Link)

from p4_mininet import P4Switch, P4Host
import apptopo
import appcontroller

parser = argparse.ArgumentParser(description='Mininet demo')
parser.add_argument('--behavioral-exe', help='Path to behavioral executable',
                    type=str, action="store", required=True)
parser.add_argument('--thrift-port', help='Thrift server port for table updates',
                    type=int, action="store", default=9090)
parser.add_argument(
    '--bmv2-log', help='verbose messages in log file', action="store_true")
parser.add_argument('--cli', help="start the mininet cli", action="store_true")
parser.add_argument('--auto-control-plane',
                    help='enable automatic control plane population', action="store_true")
parser.add_argument('--json', help='Path to JSON config file',
                    type=str, action="store", required=True)
parser.add_argument('--pcap-dump', help='Dump packets on interfaces to pcap files',
                    action="store_true")
parser.add_argument('--manifest', '-m', help='Path to manifest file',
                    type=str, action="store", required=True)
parser.add_argument('--target', '-t', help='Target in manifest file to run',
                    type=str, action="store", required=True)
parser.add_argument('--log-dir', '-l', help='Location to save output to',
                    type=str, action="store", required=True)
parser.add_argument('--cli-message', help='Message to print before starting CLI',
                    type=str, action="store", required=False, default=False)


args = parser.parse_args()


next_thrift_port = args.thrift_port


def run_command(command):
    return os.WEXITSTATUS(os.system(command))

def configureP4Switch(**switch_args):
    class ConfiguredP4Switch(P4Switch):
        def __init__(self, *opts, **kwargs):
            global next_thrift_port
            kwargs.update(switch_args)
            kwargs['thrift_port'] = next_thrift_port
            next_thrift_port += 1
            P4Switch.__init__(self, *opts, **kwargs)
    return ConfiguredP4Switch

def startNAT( root, inetIntf='eth0', subnet='10.0/8' ):
    """Start NAT/forwarding between Mininet and external network
    root: node to access iptables from
    inetIntf: interface for internet access
    subnet: Mininet subnet (default 10.0/8)="""

    # Identify the interface connecting to the mininet network
    localIntf =  root.defaultIntf()

    # Flush any currently active rules
    root.cmd( 'iptables -F' )
    root.cmd( 'iptables -t nat -F' )

    # Create default entries for unmatched traffic
    root.cmd( 'iptables -P INPUT ACCEPT' )
    root.cmd( 'iptables -P OUTPUT ACCEPT' )
    root.cmd( 'iptables -P FORWARD DROP' )

    # Configure NAT
    root.cmd( 'iptables -I FORWARD -i', localIntf, '-d', subnet, '-j DROP' )
    root.cmd( 'iptables -A FORWARD -i', localIntf, '-s', subnet, '-j ACCEPT' )
    root.cmd( 'iptables -A FORWARD -i', inetIntf, '-d', subnet, '-j ACCEPT' )
    root.cmd( 'iptables -t nat -A POSTROUTING -o ', inetIntf, '-j MASQUERADE' )

    # Instruct the kernel to perform forwarding
    root.cmd( 'sysctl net.ipv4.ip_forward=1' )

def stopNAT( root ):
    """Stop NAT/forwarding between Mininet and external network"""
    # Flush any currently active rules
    root.cmd( 'iptables -F' )
    root.cmd( 'iptables -t nat -F' )

    # Instruct the kernel to stop forwarding
    root.cmd( 'sysctl net.ipv4.ip_forward=0' )

def fixNetworkManager( root, intf ):
    """Prevent network-manager from messing with our interface,
       by specifying manual configuration in /etc/network/interfaces
       root: a node in the root namespace (for running commands)
       intf: interface name"""
    cfile = '/etc/network/interfaces'
    line = '\niface %s inet manual\n' % intf
    config = open( cfile ).read()
    if ( line ) not in config:
        print '*** Adding', line.strip(), 'to', cfile
        with open( cfile, 'a' ) as f:
            f.write( line )
        # Probably need to restart network-manager to be safe -
        # hopefully this won't disconnect you
        root.cmd( 'service network-manager restart' )

def connectToInternet( network, switch='s1', rootip='10.254', subnet='10.0/8'):
    """Connect the network to the internet
       switch: switch to connect to root namespace
       rootip: address for interface in root namespace
       subnet: Mininet subnet"""
    switch = network.get( switch )
    prefixLen = subnet.split( '/' )[ 1 ]

    # Create a node in root namespace
    root = Node( 'root', inNamespace=False )

    # Prevent network-manager from interfering with our interface
    fixNetworkManager( root, 'root-eth0' )

    # Create link between root NS and switch
    link = network.addLink( root, switch )
    link.intf1.setIP( rootip, prefixLen )

    # Start network that now includes link to root namespace
    network.start()

    # Start NAT and establish forwarding
    startNAT( root )

    # Establish routes from end hosts
    for host in network.hosts:
        host.cmd( 'ip route flush root 0/0' )
        host.cmd( 'route add -net', subnet, 'dev', host.defaultIntf() )
        host.cmd( 'route add default gw', rootip )

    return root

def main():
    with open(args.manifest, 'r') as f:
        manifest = json.load(f)

    conf = manifest['targets'][args.target]
    params = conf['parameters'] if 'parameters' in conf else {}

    os.environ.update(
        dict(map(lambda (k, v): (k, str(v)), params.iteritems())))

    def formatParams(s):
        for param in params:
            s = re.sub('\$' + param + '(\W|$)', str(params[param]) + r'\1', s)
            s = s.replace('${' + param + '}', str(params[param]))
        return s

    AppTopo = apptopo.AppTopo
    AppController = appcontroller.AppController

    if not os.path.isdir(args.log_dir):
        if os.path.exists(args.log_dir):
            raise Exception('Log dir exists and is not a dir')
        os.mkdir(args.log_dir)
    os.environ['P4APP_LOGDIR'] = args.log_dir

    bmv2_log = args.bmv2_log or ('bmv2_log' in conf and conf['bmv2_log'])
    pcap_dump = args.pcap_dump or ('pcap_dump' in conf and conf['pcap_dump'])

    # topo = AppTopo(links, latencies, manifest=manifest, target=args.target,
    #                log_dir=args.log_dir, bws=bws)
    switchClass = configureP4Switch(
        sw_path=args.behavioral_exe,
        json_path=args.json,
        log_console=bmv2_log,
        pcap_dump=pcap_dump)
    net = Mininet(topo=None,controller=RemoteController, build=False)

    info( '*** Add controller\n')
    c0 = net.addController('c0')

    info( '*** Add switches\n')
    s1 = net.addSwitch('s1')
    s2 = net.addSwitch('s2', switchClass, log_file="logs/s2.log")

    info( '*** Add hosts\n')
    h1 = net.addHost('h1', P4Host)

    info( '*** Add links\n')
    net.addLink(h1, s1, addr1="10:00:00:00:01:01", addr2="20:00:00:00:01:01")
    net.addLink(h1, s2, addr1="10:00:00:00:01:02", addr2="20:00:00:00:02:01")

    h1.cmd('ifconfig h1-eth0 "10.1.1.1" hw ether "10:00:00:00:01:01"')
    h1.cmd('ifconfig h1-eth1 "10.1.1.2" hw ether "10:00:00:00:01:02"')

    info( '*** Add nat\n')
    rootnode = connectToInternet( net )

    sleep(1)

    subprocess.call("ovs-vsctl set-fail-mode s1 standalone".split(" "))

    controller = None
    if args.auto_control_plane or 'controller_module' in conf:
        controller = AppController(manifest=manifest, target=args.target, net=net)
        controller.start()

    for h in net.hosts:
        try: h.describe()
        except: pass

    if args.cli_message is not None:
        with open(args.cli_message, 'r') as message_file:
            print message_file.read()

    CLI(net)

    stdout_files = dict()
    return_codes = []
    host_procs = []

    def formatCmd(cmd):
        for h in net.hosts:
            cmd = cmd.replace(h.name, h.defaultIntf().updateIP())
        return cmd

    def _wait_for_exit(p, host):
        print p.communicate()
        if p.returncode is None:
            p.wait()
            print p.communicate()
        return_codes.append(p.returncode)
        if host_name in stdout_files:
            stdout_files[host_name].flush()
            stdout_files[host_name].close()

    print '\n'.join(map(lambda (k, v): "%s: %s" % (k, v), params.iteritems())) + '\n'

    for host_name in sorted(conf['hosts'].keys()):
        host = conf['hosts'][host_name]
        if 'cmd' not in host:
            continue

        h = net.get(host_name)
        stdout_filename = os.path.join(args.log_dir, h.name + '.stdout')
        stdout_files[h.name] = open(stdout_filename, 'w')
        cmd = formatCmd(host['cmd'])
        print h.name, cmd
        p = h.popen(
            cmd, stdout=stdout_files[h.name], shell=True, preexec_fn=os.setpgrp)
        if 'startup_sleep' in host:
            sleep(host['startup_sleep'])

        if 'wait' in host and host['wait']:
            _wait_for_exit(p, host_name)
        else:
            host_procs.append((p, host_name))

    for p, host_name in host_procs:
        if 'wait' in conf['hosts'][host_name] and conf['hosts'][host_name]['wait']:
            _wait_for_exit(p, host_name)

    for p, host_name in host_procs:
        if 'wait' in conf['hosts'][host_name] and conf['hosts'][host_name]['wait']:
            continue
        if p.returncode is None:
            run_command('pkill -INT -P %d' % p.pid)
            sleep(0.2)
            # check if it's still running
            rc = run_command('pkill -0 -P %d' % p.pid)
            if rc == 0:  # the process group is still running, send TERM
                sleep(1)  # give it a little more time to exit gracefully
                run_command('pkill -TERM -P %d' % p.pid)
        _wait_for_exit(p, host_name)

    if 'after' in conf and 'cmd' in conf['after']:
        cmds = conf['after']['cmd'] if type(conf['after']['cmd']) == list else [
            conf['after']['cmd']]
        for cmd in cmds:
            os.system(cmd)

    if controller:
        controller.stop()

    stopNAT( rootnode )
    net.stop()

    bad_codes = [rc for rc in return_codes if rc != 0]
    if len(bad_codes):
        sys.exit(1)


if __name__ == '__main__':
    setLogLevel('info')
    main()
