rm -rf build
mkdir build
cd build
cmake ..
make
cd bin
if ./tests ; then
    echo "Tests succeeded!"
    CL_LOG_ERRORS=stdout time ./parallel_raytrace ../../config.json ../../room3.dxf ../../mat.json out.aiff | tee out.dump
else
    echo "Tests failed. Skipping running the raytracer."
fi
