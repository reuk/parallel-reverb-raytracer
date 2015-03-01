from analyse_hrtf import write_file
from os.path import join

def main():
    out = []
    for i in range (0, 361, 15):
        for j in range (0, 181, 15):
            out.append([{'r': 1, 'a': i, 'e': j}, [[i, j, 0, 0, 0, 0, 0, 0], [i, j, 0, 0, 0, 0, 0, 0]]])

    header_string = """
    #include "hrtf_tests.h"
    //  [channel][azimuth][elevation]
    const std::array <std::array <std::array <VolumeType, 180>, 360>, 2> TestsNamespace::HrtfTest::HRTF_DATA =
    """
    write_file(header_string, out, join("tests", "hrtf.cpp"))

if __name__ == "__main__":
    main()
