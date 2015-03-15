cd build
if make ; then
    cd bin
    if ./tests ; then
        echo "Tests succeeded!"
    #    CL_LOG_ERRORS=stdout time ./parallel_raytrace ../../assets/config_hrtf.json ../../assets/room3.dxf ../../assets/mat.json out.aiff | tee out.dump
    #    CL_LOG_ERRORS=stdout time ./parallel_raytrace ../../assets/stonehenge.json ../../assets/test_models/stonehenge.obj ../../assets/mat.json out.aiff | tee out.dump
        CL_LOG_ERRORS=stdout time ./parallel_raytrace ../../assets/bedroom.json ../../assets/test_models/bedroom.obj ../../assets/mat.json out.aiff | tee out.dump
    #    CL_LOG_ERRORS=stdout time ./parallel_raytrace ../../assets/large_square.json ../../assets/test_models/large_square.obj ../../assets/mat.json out.aiff | tee out.dump
    else
        echo "Tests failed. Skipping running the raytracer."
    fi
fi
