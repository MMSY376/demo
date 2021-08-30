package.path = package.path ..";?.lua;test/?.lua;app/?.lua;"

require "Pktgen"

printf("Lua Version      : %s\n", pktgen.info.Lua_Version);
printf("Pktgen Version   : %s\n", pktgen.info.Pktgen_Version);
printf("Pktgen Copyright : %s\n", pktgen.info.Pktgen_Copyright);
prints("pktgen.info", pktgen.info);
printf("Port Count %d\n", pktgen.portCount());
printf("Total port Count %d\n", pktgen.totalPorts());

pktgen.pause("About to do range\n", 1000);
pktgen.page("range");

pktgen.dst_ip("1", "start", "211.0.0.0");
pktgen.dst_ip("1", "inc", "0.0.0.1");
pktgen.dst_ip("1", "min", "211.0.0.0");
pktgen.dst_ip("1", "max", "211.0.0.0");

pktgen.dst_port("1", "start", 0);
pktgen.dst_port("1", "inc", 0);
pktgen.dst_port("1", "min", 0);
pktgen.dst_port("1", "max", 0);

pktgen.src_ip("1", "start", "0.0.0.0");
pktgen.src_ip("1", "inc", "0.0.0.1");
pktgen.src_ip("1", "min", "0.0.0.0");
pktgen.src_ip("1", "max", "0.255.255.255");

pktgen.src_port("1", "start", 0);
pktgen.src_port("1", "inc", 0);
pktgen.src_port("1", "min", 0);
pktgen.src_port("1", "max", 0);

pktgen.pkt_size("1", "start", 64);
pktgen.pkt_size("1", "inc", 0);
pktgen.pkt_size("1", "min", 64);
pktgen.pkt_size("1", "max", 64);

pktgen.dst_ip("2", "start", "211.0.0.0");
pktgen.dst_ip("2", "inc", "0.0.0.1");
pktgen.dst_ip("2", "min", "211.0.0.0");
pktgen.dst_ip("2", "max", "211.0.0.0");

pktgen.dst_port("2", "start", 0);
pktgen.dst_port("2", "inc", 0);
pktgen.dst_port("2", "min", 0);
pktgen.dst_port("2", "max", 0);

pktgen.src_ip("2", "start", "0.0.0.0");
pktgen.src_ip("2", "inc", "0.0.0.1");
pktgen.src_ip("2", "min", "0.0.0.0");
pktgen.src_ip("2", "max", "0.255.255.255");

pktgen.src_port("2", "start", 0);
pktgen.src_port("2", "inc", 0);
pktgen.src_port("2", "min", 0);
pktgen.src_port("2", "max", 0);

pktgen.pkt_size("2", "start", 64);
pktgen.pkt_size("2", "inc", 0);
pktgen.pkt_size("2", "min", 64);
pktgen.pkt_size("2", "max", 64);

pktgen.set_range("all", "on");

pktgen.start("all");
pktgen.page("main");
