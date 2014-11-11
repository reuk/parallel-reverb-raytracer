rm -rf build
mkdir build
cd build
cmake ..
make
cd bin
if ./tests ; then
    echo "Tests succeeded!"
    CL_LOG_ERRORS=stdout time ./parallel_raytrace | tee out.dump
else
    echo "Tests failed. Skipping running the raytracer."
fi
