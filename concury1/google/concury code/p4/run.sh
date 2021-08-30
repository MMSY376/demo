sudo killall controller ; sudo rm -rf build/logs/; sudo mn -c
P4APPRUNNER=./utils/p4apprunner.py
mkdir -p build
tar -czf build/p4app.tgz * --exclude='build'
sudo python $P4APPRUNNER p4app.tgz --build-dir ./build
