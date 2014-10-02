rm -rf build
mkdir build
cd build
cmake ..
make
cd bin
./parallel_raytrace > out.dump
