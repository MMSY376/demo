g++ -DNDEBUG -DDIST -DNAME=\"concury\" -DVIP_NUM=1 -DCONN_NUM=166777216 farmhash/farmhash.cc md5/md5.cpp concury.common.cpp common.cpp concury.cpp -o bin/concury.dist  -lstdc++ --std=c++17 -march=native -lpthread -O3
echo concury.dist

for name in concury silkroad maglevx; do
	for ((i=16777216; i <= 16777216 ; i=i*2)); do
		if [ "$name" = 'concury' ]; then
			g++ -DNDEBUG -DNAME=\"$name\" -DVIP_NUM=128 -DCONN_NUM=$i farmhash/farmhash.cc md5/md5.cpp concury.common.cpp common.cpp concury.cpp -o bin/$name.$i.debug  -lstdc++ --std=c++17 -march=native -lpthread -O0 -ggdb -mavx -maes
		else
			g++ -DNDEBUG -DNAME=\"$name\" -DVIP_NUM=128 -DCONN_NUM=$i farmhash/farmhash.cc common.cpp $name.cpp -o bin/$name.$i.debug  -lstdc++ --std=c++17 -march=native -lpthread -O0 -ggdb -mavx -maes
		fi;
		echo $name.$i.debug
	done
done

for name in concury silkroad maglevx; do
	for ((i=1024; i <= 16777216 ; i=i*2)); do
		if [ "$name" = 'concury' ]; then
			g++ -DNDEBUG -DNAME=\"$name\" -DVIP_NUM=128 -DCONN_NUM=$i farmhash/farmhash.cc md5/md5.cpp concury.common.cpp common.cpp concury.cpp -o bin/$name.$i  -lstdc++ --std=c++17 -march=native -lpthread -O3 -mavx -maes
			g++ -DFIX_DIP_NUM -DNDEBUG -DNAME=\"$name\" -DVIP_NUM=128 -DCONN_NUM=$i farmhash/farmhash.cc md5/md5.cpp concury.common.cpp common.cpp concury.cpp -o bin/$name.$i.fix  -lstdc++ --std=c++17 -march=native -lpthread -O3 -mavx -maes
		else
			g++ -DNDEBUG -DNAME=\"$name\" -DVIP_NUM=128 -DCONN_NUM=$i farmhash/farmhash.cc common.cpp $name.cpp -o bin/$name.$i  -lstdc++ --std=c++17 -march=native -lpthread -O3 -mavx -maes
			g++ -DFIX_DIP_NUM -DNDEBUG -DNAME=\"$name\" -DVIP_NUM=128 -DCONN_NUM=$i farmhash/farmhash.cc common.cpp $name.cpp -o bin/$name.$i.fix  -lstdc++ --std=c++17 -march=native -lpthread -O3 -mavx -maes
		fi;
		echo $name.$i
	done
done
