#include<stdlib.h>
#include<string.h>
#include<stdint.h>

static uint8_t m_sys_buff[16] = {0,0};
static uint8_t m_sys_buff_length = 100;
static uint8_t m_temp_buff[16] = {0,0};


static void test_klockwork(void)
{
    memcpy(m_sys_buff,m_temp_buff,22);
    memset(m_sys_buff,0,22);
    uint8_t* buff = (uint8_t *)malloc(5);

    memcpy(m_sys_buff,m_temp_buff,m_sys_buff_length);
    memset(m_sys_buff,0,m_sys_buff_length);

    static uint8_t m_sys_buff_length_test = 100;
    memcpy(m_sys_buff,m_temp_buff,m_sys_buff_length_test);
    memset(m_sys_buff,0,m_sys_buff_length_test);
}

static uint8_t* test_klockwork_B(void)
{
	uint8_t* buff = (uint8_t *)malloc(5);
    return buff;
}

static void test_klockwork_C(uint8_t* buff)
{
	free(buff);
}

uint32_t sys_info_init(void)
{

        printf("===###===");
	m_sys_buff_length = 10;
	test_klockwork();
	m_sys_buff_length = 100;
	test_klockwork();

	uint8_t* buff = test_klockwork_B();
	free(buff);

	uint8_t* buff2 = test_klockwork_B();
	test_klockwork_C(buff2);

	uint8_t* buff3 = test_klockwork_B();

    return 0;
}

int main()
{
   printf("aaabbbccc");
   sys_info_init();
   test_klockwork();

   return 0;
}
