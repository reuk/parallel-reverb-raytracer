cd build
make
cd bin
if ./tests ; then
    echo "Tests succeeded!"
    CL_LOG_ERRORS=stdout time ./parallel_raytrace ../../assets/config_hrtf.json ../../assets/room3.dxf ../../assets/mat.json out.aiff | tee out.dump
else
    echo "Tests failed. Skipping running the raytracer."
fi
