# 个人课程项目介绍
写了一个简易的多线程小程序验证经典的理发店分配问题。 不借助信号量（semaphore） 仅通过互斥锁（mutex）保证指针所指向数据的原子性，实施了一个小的队列数据结构来保证数据产生的先后，加之条件变量（cond)确保同步。特别地，所有的条件都在while循环中避免了虚假唤醒（spurious wakeup). 具体的伪代码和分析报告请见`report.pdf`.

# How to run:
1. compile the C file `gcc -o barber_shop barber_shop.c`
2. run `./barber_shop`
3. follow the prompts to tune params
4. check the output manually perhaps?

# For you convenience:
There are two terminal output logs bundled. 
`barberfaster.log` for barbers working faster than customer arriving rate.
`barberslower.log` for the other way.
Each log has 15 test cases tuned with different combinations of number parameters. For more details, please check the report testing section.
