# WDFuzzer Manual

##### WDFuzzer：winafl + drmemory

WDFuzzer是一款适用于windows系统的应用级模糊测试工具，它结合了[winafl](https://github.com/googleprojectzero/winafl)和[drmemory](https://github.com/DynamoRIO/drmemory)，在winafl基于覆盖率进行模糊测试的同时，使用drmemory提供运行时内存检查，发现模糊测试过程中触发的内存漏洞。

##### 动机

模糊测试作为一种比较成熟的技术，在各个平台的软件上发现了大量的漏洞。在linux平台上，用户可以轻松使用AFL对应用程序进行模糊测试，同时使用memcheck、ASan等工具在模糊测试中检测目标程序的内存漏洞。在windows平台上，尽管有AFL对应的windows解决方案——winafl的存在，但是，winafl并不是简单的应用级模糊测试解决方案，而是一种函数级模糊测试工具，这意味着在使用winafl之前需要对目标程序有一定的了解。同时，windows平台上还没有能够检测内存漏洞的模糊测试工具。

出于以上动机，WDFuzzer将winafl和内存检测工具drmemory进行了结合，并对winafl进行了流程上的修改，使得WDFuzzer成为了一个能够检测内存漏洞、可方便使用的应用级模糊测试工具。

##### 和现有工具的不同

+ **应用级模糊测试**：windows平台现有模糊测试工具大多为*函数级*模糊测试工具，使用起来比较繁琐，需要对目标程序源代码有一定了解或通过逆向分析来找到目标函数，而后对函数单独循环模糊测试。WDFuzzer是一款更为简单、易用的模糊测试工具，可直接对整个应用程序进行模糊测试，无论目标程序会不会自动退出，都可以使用WDFuzzer进行模糊测试。
+ **内存检查**：使用drmemory对目标程序进行内存检查，提高了模糊测试过程中发现程序内存漏洞的能力。在实际使用过程中，也可以选择不使用内存检查功能，以提高模糊测试速率。

##### 现有缺陷

如果要使用内存检查功能，那么可能会有如下缺陷：

+ 需要目标程序的**调试信息**。drmemory只能检测有调试信息的目标程序，如果要启用drmemory进行内存检测，请按照drmemory[文档](https://dynamorio.org/drmemory_docs/page_prep.html)中的指示对目标程序进行编译。
+ 对**GUI程序**进行模糊测试的时候，可能会有误报，这是drmemory的缺陷。如果在启用drmemory进行模糊测试时，请注意这一点。

## 快速开始

如果不需要进行二次开发，可以直接下载`release`进行模糊测试，`release`中有一个`demo`和相应的说明文档，可以让你快速熟悉`WDFuzzer`。

## 编译WDFuzzer/二次开发

二次开发时，根据需求对winafl和drmemory进行修改即可。

编译过程其实就是两个工具分开编译的过程。详细编译过程见它们的文档，这里以32位程序为例做简要介绍。

##### 准备环境

+ 安装MS Visual Studio 2017。
+ 克隆本仓库。

##### 编译drmemory

打开MS Visual Studio 2017的x86命令行，执行以下命令：

````
cd drmemory
mkdir build32
cd build32
cmake ..
cmake  --build . --config RelWithDebInfo
````

编译完成后，**Dynamorio**根目录为`drmemory\build32\dynamorio`。

##### 编译winafl

````
cd winafl
mkdir build32
cd build32
cmake -G"Visual Studio 15 2017" .. -DDynamoRIO_DIR=[Dynamorio根目录]\cmake
cmake --build . --config Release
````

##### 注意事项

+  drmemory 编译过程可能时间较长，中途可能会有一些 warning 甚至 error ，通常情况下，这不会影响最终文件的生成。

+ 如果编译顺利，你将在`winafl\build32\bin\Release`目录下看到**afl-fuzz.exe**和**winafl.dll**等文件，在`winfuzz\drmemory\build32\bin`目录下看到**drmemory.exe**等可执行文件。

## 使用WDFuzzer进行模糊测试

### 命令

````
afl-fuzz.exe [afl选项] -- [drmemory选项] -- [调用目标程序的命令]
````

##### afl选项

(`-`表示该选项必须使用，`+`表示该选项可按照实际使用过程指定)

````
-i [目录]         - 指定存放了初始测例的目录
-o [目录]         - 指定存放测试结果的目录
-t [毫秒]   	   	- 每一次测试最长的时间
-D [目录]        	- Dynamorio可执行文件存放目录，该目录下应有drrun.exe,drconfig.exe
-R [目录]		    + drmemory可执行文件存放目录，该目录下应有drmemory.exe
				   若需要使用drmemory在模糊测试过程中进行内存检查，则必须指定-R选项
-O [目录]         + 目标程序输出文件所在目录
				   若目标程序每一轮执行都会创建新文件，可通过-O选项指定目标文件夹，从而及时清理
-N               + 目标程序不会自动停止时，需要使用-N选项
````

若需要使用其余afl选项，参见[winafl github主页](https://github.com/googleprojectzero/winafl)。

##### drmemory选项

(`-`表示该选项必须使用，`+`表示该选项可按照实际使用过程指定)

````
-coverage_mode [edge|bb]		 - 指定覆盖率计算模式，计算边缘覆盖率或是基本块覆盖率
-coverage_module [module]	     - 指定关注的模块，可以通过多次使用该选项来指定多个模块
-no_check_uninitialized			 + 不检查访问未初始化的内存错误
								   对部分目标程序进行检测时，该功能可能误报，根据实际目标程序决定。
````

##### 调用目标程序的命令

注意使用`@@`替代输入文件，若目标程序每一次运行会产生输出文件，且上次输出可能影响下一次运行，在命令中指定目标程序的输出文件目录。在afl选项中使用`-O`选项指定相同的目录，以在每次运行前对该目录进行清理。

### 注意

drmemory在运行时可能要预加载系统组件的符号文件，为了保证测试过程正确，请先单独运行`drmemory.exe`对目标程序进行内存检测，确认测试过程中正常、没有误报、不会崩溃退出后再进行模糊测试。单独使用`drmemory.exe`对目标程序进行内存检测的命令见例子部分。

### 例子

文件目录结构：

````
-WDFuzzer
	-winafl
		-build32\bin\Release\afl-fuzz.exe
	-drmemory
		-build32
			-bin\drmemory.exe
			-dynamorio\bin32
	-test
		-in
		-out
		-target_out
		-target.exe
````

模糊测试命令：

````
cd WDFuzzer\test

..\winafl\build32\bin\Release\afl-fuzz.exe -i in -o out -t 10000 -D ..\drmemory\build32\dynamorio\bin32 -R ..\drmemory\build32\bin -O target_out -N -- -coverage_module target.exe -coverage_module target_1.dll -coverage_mode edge -- target.exe -out target_out -in @@
````

若在模糊测试中，不需要使用drmemory进行内存检查，则不使用`-R`选项和drmemory选项即可：

````
..\winafl\build32\bin\Release\afl-fuzz.exe -i in -o out -t 10000 -D ..\drmemory\build32\dynamorio\bin32 -O target_out -N -- -- target.exe -out target_out -in @@
````

单独使用drmemory进行内存检查的命令：

````
..\drmemory\build32\bin\drmemory.exe -batch -fuzzer_id any -coverage_module target_1.dll -coverage_mode edge -single -- target.exe -out target_out -in any_input
````

该命令中，必须使用`-single`选项，表示单独运行`drmemory`，单独运行时请不要使用`-ignore_kernel`选项，让`drmemory`自动获取系统组件的符号信息，除`-batch -fuzzer_id any`选项之外的其他选项与模糊测试时的`drmemory`选项保持一致。

## 实现概述

WDFuzzer基于[winafl](https://github.com/googleprojectzero/winafl/)和[drmemory](https://github.com/DynamoRIO/drmemory)两种工具，是面向windows应用程序的覆盖率指导、能检测内存操作错误的应用级模糊测试工具。

+ winafl是基于afl实现的面向windows平台的覆盖率指导的函数级模糊测试工具。它基于[dynamorio](https://dynamorio.org/)实现了对目标程序的运行时插桩，以控制函数级模糊测试流程和获取覆盖率。
+ drmemory是一种跨平台内存检查工具。它基于dynamorio实现了对目标程序的运行时插桩，以进行运行时内存检查。

为了实现应用级的模糊测试和将drmemory应用到winafl的模糊测试过程中，WDFuzzer对这两个工具进行了如下改动：

+ 修改winafl，将函数级模糊测试改为应用级模糊测试
+ 修改winafl，使其能够对不会自动停止的应用程序模糊测试
+ 修改drmemory和winafl，使它们能够通过共享内存、文件、管道等方式通信
+ 修改drmemory，使其插桩记录覆盖率并写入共享内存
+ 修改drmemory，使其将发现的内存错误写入管道

![uml](pic/uml.jpg)

