outdir=impulses
mkdir -p $outdir

progname=parallel_raytrace

if command -v $progname >/dev/null 2>&1; then
    progname=$progname
elif command -v ../bin/$progname >/dev/null 2>&1; then
    progname=../bin/$progname
elif command -v ../build/bin/$progname >/dev/null 2>&1; then
    progname=../build/bin/$progname
else
    echo "Command not found!"
    exit 1
fi

callraytrace () {
    $progname assets/configs/$1.json assets/test_models/$2.obj assets/materials/$3.json $outdir/$1_$2_$3.aiff
}

callraytrace oct random_pillars mat

#callraytrace near_c echo_tunnel mat
#callraytrace far echo_tunnel mat
#
#callraytrace bedroom bedroom mat
#callraytrace near_c small_square mat
#callraytrace near_c large_pentagon mat
#callraytrace far large_pentagon mat
#
#callraytrace vault vault vault
#callraytrace vault_l vault vault
#callraytrace vault_r vault vault
#
#callraytrace hrtf_vault vault vault
#callraytrace hrtf_vault_l vault vault
#callraytrace hrtf_vault_r vault vault
#
#callraytrace near_c bedroom                 mat
#callraytrace near_l bedroom                 mat
#callraytrace near_r bedroom                 mat
#callraytrace near_c random_pillars          mat
#callraytrace near_l random_pillars          mat
#callraytrace near_r random_pillars          mat
#callraytrace medium random_pillars          mat
#callraytrace far_2 random_pillars           mat
#
#callraytrace near_c small_triangle          mat
#callraytrace near_l small_triangle          mat
#callraytrace near_r small_triangle          mat
#callraytrace near_l small_square            mat
#callraytrace near_r small_square            mat
#callraytrace near_c small_pentagon          mat
#callraytrace near_l small_pentagon          mat
#callraytrace near_r small_pentagon          mat
#callraytrace near_c small_heptagon          mat
#callraytrace near_l small_heptagon          mat
#callraytrace near_r small_heptagon          mat
#callraytrace near_c medium_triangle         mat
#callraytrace near_l medium_triangle         mat
#callraytrace near_r medium_triangle         mat
#callraytrace near_c medium_square           mat
#callraytrace near_l medium_square           mat
#callraytrace near_r medium_square           mat
#callraytrace near_c medium_pentagon         mat
#callraytrace near_l medium_pentagon         mat
#callraytrace near_r medium_pentagon         mat
#callraytrace near_c medium_heptagon         mat
#callraytrace near_l medium_heptagon         mat
#callraytrace near_r medium_heptagon         mat
#callraytrace near_c large_triangle          mat
#callraytrace near_l large_triangle          mat
#callraytrace near_r large_triangle          mat
#callraytrace near_c large_square            mat
#callraytrace near_l large_square            mat
#callraytrace near_r large_square            mat
#callraytrace near_c large_pentagon          mat
#callraytrace near_l large_pentagon          mat
#callraytrace near_r large_pentagon          mat
#callraytrace near_c large_heptagon          mat
#callraytrace near_l large_heptagon          mat
#callraytrace near_r large_heptagon          mat
#callraytrace medium medium_triangle         mat
#callraytrace medium medium_square           mat
#callraytrace medium medium_pentagon         mat
#callraytrace medium medium_heptagon         mat
#callraytrace medium large_triangle          mat
#callraytrace medium large_square            mat
#callraytrace medium large_pentagon          mat
#callraytrace medium large_heptagon          mat
#callraytrace far large_triangle             mat
#callraytrace far large_square               mat
#callraytrace far large_pentagon             mat
#callraytrace far large_heptagon             mat
#
#callraytrace near_c small_triangle          damped
#callraytrace near_l small_triangle          damped
#callraytrace near_r small_triangle          damped
#callraytrace near_c small_square            damped
#callraytrace near_l small_square            damped
#callraytrace near_r small_square            damped
#callraytrace near_c small_pentagon          damped
#callraytrace near_l small_pentagon          damped
#callraytrace near_r small_pentagon          damped
#callraytrace near_c small_heptagon          damped
#callraytrace near_l small_heptagon          damped
#callraytrace near_r small_heptagon          damped
#callraytrace near_c medium_triangle         damped
#callraytrace near_l medium_triangle         damped
#callraytrace near_r medium_triangle         damped
#callraytrace near_c medium_square           damped
#callraytrace near_l medium_square           damped
#callraytrace near_r medium_square           damped
#callraytrace near_c medium_pentagon         damped
#callraytrace near_l medium_pentagon         damped
#callraytrace near_r medium_pentagon         damped
#callraytrace near_c medium_heptagon         damped
#callraytrace near_l medium_heptagon         damped
#callraytrace near_r medium_heptagon         damped
#callraytrace near_c large_triangle          damped
#callraytrace near_l large_triangle          damped
#callraytrace near_r large_triangle          damped
#callraytrace near_c large_square            damped
#callraytrace near_l large_square            damped
#callraytrace near_r large_square            damped
#callraytrace near_c large_pentagon          damped
#callraytrace near_l large_pentagon          damped
#callraytrace near_r large_pentagon          damped
#callraytrace near_c large_heptagon          damped
#callraytrace near_l large_heptagon          damped
#callraytrace near_r large_heptagon          damped
#callraytrace medium medium_triangle         damped
#callraytrace medium medium_square           damped
#callraytrace medium medium_pentagon         damped
#callraytrace medium medium_heptagon         damped
#callraytrace medium large_triangle          damped
#callraytrace medium large_square            damped
#callraytrace medium large_pentagon          damped
#callraytrace medium large_heptagon          damped
#callraytrace far large_triangle             damped
#callraytrace far large_square               damped
#callraytrace far large_pentagon             damped
#callraytrace far large_heptagon             damped
