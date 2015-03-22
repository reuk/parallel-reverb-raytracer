callraytrace () {
    CL_LOG_ERRORS=stdout time ./parallel_raytrace ../../demo/assets/configs/$1.json ../../demo/assets/test_models/$2.obj ../../demo/assets/materials/$3.json $1_$2_$3.aiff | tee out.dump
}

cd build
if make ; then
    cd bin
    if CL_LOG_ERRORS=stdout ./tests ; then
        echo "Tests succeeded!"
        #callraytrace bedroom bedroom mat
        #callraytrace near_c small_square mat
        #callraytrace near_c large_pentagon mat
        #callraytrace far large_pentagon mat
        callraytrace vault vault vault
    else
        echo "Tests failed. Skipping running the raytracer."
    fi
fi
