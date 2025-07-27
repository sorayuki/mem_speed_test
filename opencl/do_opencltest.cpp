#include <stdio.h>
#include <string>
#include <regex>
#include <boost/asio.hpp>
#include <boost/asio/readable_pipe.hpp>
#include <boost/asio/writable_pipe.hpp>
#include <boost/process/v2/process.hpp>
#include <boost/process/v2/stdio.hpp>

double do_test(const std::string& inputs) {
    using namespace boost::process::v2;
    using namespace boost::asio;

    io_context io_context;

    readable_pipe rp{io_context};
    writable_pipe wp{io_context};
    std::string outputStr;
    
    // 创建进程，配置stdin和stdout
    process proc{io_context, "colorconv.exe", {}, process_stdio{ wp, {}, rp } };
    write(wp, buffer(inputs));
    wp.close();
    try {
        read(rp, dynamic_buffer(outputStr));
    } catch(...) {}

    // 等待进程结束
    proc.wait();

    std::regex re(R"__(avg = (\d+(.\d+)))__");
    std::regex_iterator<std::string::iterator> it(outputStr.begin(), outputStr.end(), re), it_end{};
    double retval = 0.0;
    while (it != it_end) {
        if (it->size() >= 1) {
            // 提取匹配的数字部分
            retval = std::stod((*it)[1].str());
        }
        ++it;
    }
    
    return retval;
}

#include <iostream>
int main() {
    // device / buffer type / reuse buffer object / buffer copy mode / memcpy impl / host memory mode / use pipeline
    std::cout << "|Regular|Device  |Yes       |R/W Buf |No pipeline    |" << do_test("0 0 1 0   0 0") << "|" << std::endl;
    std::cout << "|Regular|Device  |Yes       |Map     |std            |" << do_test("0 0 1 1 0 0  ") << "|" << std::endl;
    std::cout << "|Regular|Device  |Yes       |Map     |no copy        |" << do_test("0 0 1 1 1 0  ") << "|" << std::endl;
    std::cout << "|Regular|Device  |Yes       |Map     |parallel       |" << do_test("0 0 1 1 2 0  ") << "|" << std::endl;
    std::cout << "|Regular|Device  |Yes       |R/W Buf |Pipeline       |" << do_test("0 0 1 0   0 1") << "|" << std::endl;
    std::cout << "|Regular|Device  |No        |R/W Buf |No pipeline    |" << do_test("0 0 0 0   0 0") << "|" << std::endl;
    std::cout << "|Regular|Host    |Yes       |R/W Buf |No pipeline    |" << do_test("0 2 1 0   0 0") << "|" << std::endl;
    std::cout << "|Regular|Host    |Yes       |Map     |std            |" << do_test("0 2 1 1 0 0  ") << "|" << std::endl;
    std::cout << "|Regular|Host    |Yes       |Map     |no copy        |" << do_test("0 2 1 1 1 0  ") << "|" << std::endl;
    std::cout << "|Regular|Host    |Yes       |Map     |parallel       |" << do_test("0 2 1 1 2 0  ") << "|" << std::endl;
    std::cout << "|Regular|Host    |Yes       |R/W Buf |Pipeline       |" << do_test("0 2 1 0   0 0") << "|" << std::endl;
    std::cout << "|Regular|Host    |No        |R/W Buf |No pipeline    |" << do_test("0 2 0 0   0 0") << "|" << std::endl;
    std::cout << "|Regular|SVM     |Yes       |R/W Buf |No pipeline    |" << do_test("0 1 1 0   0 0") << "|" << std::endl;
    std::cout << "|Regular|SVM     |Yes       |Map     |std            |" << do_test("0 1 1 1 0 0  ") << "|" << std::endl;
    std::cout << "|Regular|SVM     |Yes       |Map     |no copy        |" << do_test("0 1 1 1 1 0  ") << "|" << std::endl;
    std::cout << "|Regular|SVM     |Yes       |Map     |parallel       |" << do_test("0 1 1 1 2 0  ") << "|" << std::endl;
    std::cout << "|Regular|SVM     |Yes       |R/W Buf |Pipeline       |" << do_test("0 1 1 0   0 1") << "|" << std::endl;
    std::cout << "|Regular|SVM     |No        |R/W Buf |No pipeline    |" << do_test("0 1 0 0   0 0") << "|" << std::endl;
    std::cout << "|Regular|UseHost |No        |/       |No pipeline    |" << do_test("0 3       0 0") << "|" << std::endl;
    std::cout << "|Aligned|UseHost |No        |/       |No pipeline    |" << do_test("0 3       1 0") << "|" << std::endl;
    std::cout << "|Pinned |Device  |Yes       |R/W Buf |No pipeline    |" << do_test("0 0 1 0   2 0") << "|" << std::endl;
    std::cout << "|Pinned |Device  |Yes       |R/W Buf |pipeline       |" << do_test("0 0 1 0   2 1") << "|" << std::endl;
    std::cout << "|Pinned |Host    |Yes       |R/W Buf |No pipeline    |" << do_test("0 2 1 0   2 0") << "|" << std::endl;
    std::cout << "|Pinned |Host    |Yes       |R/W Buf |pipeline       |" << do_test("0 2 1 0   2 1") << "|" << std::endl;
    std::cout << "|Pinned |SVM     |Yes       |R/W Buf |No pipeline    |" << do_test("0 1 1 0   2 0") << "|" << std::endl;
    std::cout << "|Pinned |SVM     |Yes       |R/W Buf |pipeline       |" << do_test("0 1 1 0   2 1") << "|" << std::endl;
    return 0;
}

