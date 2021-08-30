import subprocess
import os
from shortest_path import ShortestPath

class AppController:
    def __init__(self, manifest=None, target=None, topo=None, net=None, links=None):
        self.manifest = manifest
        self.target = target
        self.conf = manifest['targets'][target]
        self.topo = topo
        self.net = net
        self.links = links

    def read_entries(self, filename):
        entries = []
        with open(filename, 'r') as f:
            for line in f:
                line = line.strip()
                if line == '': continue
                entries.append(line)
        return entries

    def add_entries(self, thrift_port=9090, sw=None, entries=None):
        assert entries
        if sw: thrift_port = sw.thrift_port

        print '\n'.join(entries)
        p = subprocess.Popen(['simple_switch_CLI', '--thrift-port', str(thrift_port)], stdin=subprocess.PIPE)
        p.communicate(input='\n'.join(entries))

    def read_register(self, register, idx, thrift_port=9090, sw=None):
        if sw: thrift_port = sw.thrift_port
        p = subprocess.Popen(['simple_switch_CLI', '--thrift-port', str(thrift_port)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = p.communicate(input="register_read %s %d" % (register, idx))
        reg_val = filter(lambda l: ' %s[%d]' % (register, idx) in l, stdout.split('\n'))[0].split('= ', 1)[1]
        return long(reg_val)

    def start(self):
        entries = {}
        entries["s2"] = self.read_entries("s2-commands.txt")
        # if self.topo:
        #     for sw in self.topo.switches():
        #         entries[sw] = []
        #         if 'switches' in self.conf and sw in self.conf['switches'] and 'entries' in self.conf['switches'][sw]:
        #             extra_entries = self.conf['switches'][sw]['entries']
        #             if type(extra_entries) == list: # array of entries
        #                 entries[sw] += extra_entries
        #             else: # path to file that contains entries
        #                 entries[sw] += self.read_entries(extra_entries)

        print "**********"
        print "Configuring entries in p4 tables"
        for sw_name in entries:
            print
            print "Configuring switch... %s" % sw_name
            sw = self.net.get(sw_name)
            if entries[sw_name]:
                self.add_entries(sw=sw, entries=entries[sw_name])
        print "Configuration complete."
        print "**********"

        print "**********"
        print "start pox by yourself"
        print "**********"

    def stop(self):
        pass
