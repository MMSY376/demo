rm *.data
for name in concury silkroad maglevx; do
	for ((i=1024; i <= 16777216 ; i=i*2)); do
		echo ./$name.$i
		./$name.$i
		echo ./$name.$i.fix
		./$name.$i.fix
	done
done