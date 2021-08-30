ALL: concury.p4

concury.p4: 
	clang++ -DP4_CONCURY p4concury.cpp ../concury.common.cpp ../common.cpp -o build/p4concury --std=c++14 -march=native -lstdc++ -ltins -lsimpleswitch_thrift -lruntimestubs -lthrift -lpthread -lprofiler -O0 -g3 -ggdb

P: 
	clang++ -DNDEBUG -DP4_CONCURY p4concury.cpp ../concury.common.cpp ../common.cpp -o build/p4concury128 --std=c++14 -march=native -lstdc++ -ltins -lsimpleswitch_thrift -lruntimestubs -lthrift -lpthread -lprofiler -O3 -g3 -ggdb

clean:
	rm -rf host