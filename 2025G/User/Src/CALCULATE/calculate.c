#include "calculate.h"
#include "stdio.h"
#include "tim.h"
#include "usart.h"


/*
1-500kHz

*/

uint8_t dipin=0;
uint8_t flag=0;

extern uint8_t  TIM5CH2_CAPTURE_STA;		//���벶��״̬		    				
extern uint32_t	TIM5CH2_CAPTURE_VAL;	//���벶��ֵ  
extern uint32_t TIM_ExtCntFreq;


static TIM_HandleTypeDef htim4_test = {0};
static uint8_t tim4_test_inited = 0;

static void TIM4_TestInit(uint32_t psc)
{
    __HAL_RCC_TIM4_CLK_ENABLE();

    if (tim4_test_inited == 0U)
    {
        htim4_test.Instance = TIM4;
        htim4_test.Init.Prescaler = psc;
        htim4_test.Init.CounterMode = TIM_COUNTERMODE_UP;
        htim4_test.Init.Period = 0xFFFFU;
        htim4_test.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
        htim4_test.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

        if (HAL_TIM_Base_Init(&htim4_test) != HAL_OK)
        {
            Error_Handler();
        }

        tim4_test_inited = 1U;
    }
    else
    {
        HAL_TIM_Base_Stop(&htim4_test);
        __HAL_TIM_SET_PRESCALER(&htim4_test, psc);
        __HAL_TIM_SET_AUTORELOAD(&htim4_test, 0xFFFFU);
    }

    __HAL_TIM_SET_COUNTER(&htim4_test, 0U);
    __HAL_TIM_CLEAR_FLAG(&htim4_test, TIM_FLAG_UPDATE);
    htim4_test.Instance->EGR = TIM_EGR_UG;
}

//����TIM3Ƶ�ʣ���ADC1�Ĳ���Ƶ��
//f:�ź�Ƶ��
void adjust_adc_sample_freq(uint32_t f)
{

	uint32_t sample_f = f*64;// ADC采样频率，每个信号周期采64点，1024点FFT约覆盖16个周期
		if((f*64)>=640000)
		{
			sample_f=640000;
		}
		TIM3_Freq_Set(sample_f);

		HAL_Delay(50);
}


uint32_t test_f(void)
{
	uint8_t i=0;
	uint32_t temp_f=0;	
			
			TIM4_TestInit(10U - 1U);		//ֻ��2 5������ʮ��λ
			HAL_TIM_Base_Start(&htim4_test);
			HAL_Delay(500);
			
			for(i=0;i<10;i++)
			{
					while(flag==0);
					printf("%3.3lf*100kHz\n",(double)TIM_ExtCntFreq/1000.00f);
					HAL_Delay(10);
					flag=0;
					if(__HAL_TIM_GET_FLAG(&htim4_test, TIM_FLAG_UPDATE) != RESET)
					{
							printf("�����\r\n");
							printf("����6M\r\n");
							__HAL_TIM_CLEAR_FLAG(&htim4_test, TIM_FLAG_UPDATE);
							TIM4_TestInit(100U - 1U);
							HAL_TIM_Base_Start(&htim4_test);
							HAL_Delay(10);
							dipin=2;
						break;
						
					}
				if(i==9)
				{
						if(TIM_ExtCntFreq<10)
						{printf("С��1k\r\n");
							dipin=1;
						}
						
				}
				if(i>1)
					temp_f+=TIM_ExtCntFreq;
				
			}
					temp_f*=12.5;
			
			if(dipin==2)//���ߣ���pre
			{
				temp_f=0;
					for(i=0;i<10;i++)
				{
					while(flag==0);
					printf("%3.3lf MHz\n",(double)TIM_ExtCntFreq/1000.00f);
					HAL_Delay(10);
					flag=0;
					if(i>1)
					temp_f+=TIM_ExtCntFreq;
					
				}
					TIM4_TestInit(10U - 1U);//�Ļ�ȥ
				dipin=0;
				temp_f*=125;
			}
				
			
			HAL_TIM_Base_Stop(&htim4_test);
			
			
return temp_f;

}

/*
 * ��ʾ��ͬʱ�ҳ����ֵ����Сֵ
 * �����㷨ӦΪ���η� ʱ�临�Ӷ� O(3/2n)
 */

/* �ҳ���Сֵλ�� */
uint16_t Min_Float(float Mag[], uint16_t len)
{
    uint16_t i, Fn_Num;
    Fn_Num = 0;
    Mag[Fn_Num] = Mag[0];
    for (i = 1; i < len; i++)
    {
        if (Mag[Fn_Num] > Mag[i])
        {
            Fn_Num = i;
        }
    }
    return Fn_Num;
}

/* �ҳ������ڶ�Сֵλ�� */
uint16_t Min_Unsigned(uint16_t Mag[], uint16_t len)
{
    uint16_t i, Fn_Num;
    Fn_Num = 0;
    Mag[Fn_Num] = Mag[0];
    for (i = 1; i < len; i++)
    {
        if (Mag[Fn_Num] > Mag[i])
        {
            Fn_Num = i;
        }
    }
    return Fn_Num;
}

/* �ҳ����ֵλ�� */
uint16_t Max_Unsigned(uint16_t Mag[], uint16_t len)
{
    uint16_t i, Fn_Num;
    Fn_Num = 0;
    Mag[Fn_Num] = Mag[0];
    for (i = 1; i < len; i++)
    {
        if (Mag[Fn_Num] < Mag[i])
        {
            Fn_Num = i;
        }
    }
    return Fn_Num;
}

/* ��һ����Χ���ҳ����ֵλ�� */
uint16_t Max_Float_WithinRange(float Data[], uint16_t Left, uint16_t Right)
{
    uint16_t i, MaxIndex;
    MaxIndex = Left;
    for (i = Left; i <= Right; ++i)
    {
        if (Data[MaxIndex] < Data[i])
        {
            MaxIndex = i;
        }
    }
    return MaxIndex;
}

/*	������		*/
uint16_t Most_Unsigned(uint16_t Mag[], uint16_t len)
{
		uint16_t i,j,x,m;
	
		uint16_t buff[10];
		for(i=0;i<len;i++)
	{
			for(j=i+1,x=0;j<len;j++)
		{
				if(Mag[i]==Mag[j])
					x++;
		}
		buff[i]=x;
	}
	
    for(i=0,x=0,m=Mag[0]; i<len; i++)
	{
        if(buff[i]>x){
            x=buff[i];
            m=Mag[i];
        }
        else if(buff[i]==x){
        m=Mag[i]<m?Mag[i]:m;
        }
    }	
	
	
    return m;
}

/*	�жϲ��μ���Ƶ		*/
// wave����0	����1
void Demultiplex(float Mag[], uint16_t Wave_Type[], uint32_t Basic_Freq[], uint16_t len, uint32_t disting_freq)
{
	uint16_t Index[2] = {0, 0};
	uint16_t ind;
	
	float Max_Amp, Second_Amp;
	float Three_Max_Amp, Three_Second_Amp;
	float  Five_Second_Amp;
	float Max_d, Second_d;
	
	Max_SecondMax_Index(Mag, Index, 1, len);

	if(Index[0] < Index[1])
	{
		ind = Index[0];
		Index[0] = Index[1];
		Index[1] = ind;
	}
	//printf("%d,%d\r\n",Index[0],Index[1]);	
	
	Max_Amp = Mag[Index[0]];
	
	Three_Max_Amp = Mag[ Max_Float_WithinRange(Mag, 3*Index[0] - 3, 3*Index[0] + 3) ];
	
	Max_d = Max_Amp / Three_Max_Amp;
	
	if(Max_d > 30.0f) //have sin
	{
		if(Index[1] * 3 != Index[0]) //no cover
		{
			Second_Amp = Mag[Index[1]];
			Three_Second_Amp = Mag[ Max_Float_WithinRange(Mag, 3*Index[1] - 3, 3*Index[1] + 3) ];
			Second_d = Second_Amp / Three_Second_Amp;
				//printf("second:%f\r\n",Second_d);
			if(Second_d < 17.0f) //have tri
			{
				//printf("����");
				Wave_Type[0] = 1;
				Wave_Type[1] = 0;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
			}
			else
			{
				Wave_Type[0] = 0;
				Wave_Type[1] = 0;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
			}
		}
		else
		{
			Second_Amp = Mag[Index[1]];
			Five_Second_Amp = Mag[ Max_Float_WithinRange(Mag, 5*Index[1] - 3, 5*Index[1] + 3) ];
			Second_d = Second_Amp / Five_Second_Amp;
			
			if(Second_d < 40.0f) //have tri
			{
				Wave_Type[0] = 1;
				Wave_Type[1] = 0;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
			}
			else
			{
				Wave_Type[0] = 0;
				Wave_Type[1] = 0;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
			}
		}
	}
	else
	{
		if(Index[1] * 3 != Index[0]) //no cover
		{
			Second_Amp = Mag[Index[1]];
			Three_Second_Amp = Mag[ Max_Float_WithinRange(Mag, 3*Index[1] - 3, 3*Index[1] + 3) ];
			Second_d = Second_Amp / Three_Second_Amp;
			
			if(Second_d < 20.0f) //have tri
			{
				Wave_Type[0] = 1;
				Wave_Type[1] = 1;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
				
				if(Index[0] >= 159 && Index[0] <= 161 && Max_d > 25.0f)
				{
					Wave_Type[0] = 1;
					Wave_Type[1] = 0;
					Basic_Freq[0] = disting_freq * Index[1] / 1024;
					Basic_Freq[1] = disting_freq * Index[0] / 1024;
				}
			}
			else
			{
				Wave_Type[0] = 0;
				Wave_Type[1] = 1;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
			}
		}
		else
		{
			Second_Amp = Mag[Index[1]];
			Five_Second_Amp = Mag[ Max_Float_WithinRange(Mag, 5*Index[1] - 3, 5*Index[1] + 3) ];
			Second_d = Second_Amp / Five_Second_Amp;
			
			if(Second_d < 40.0f) //have tri
			{
				Wave_Type[0] = 1;
				Wave_Type[1] = 1;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
			}
			else
			{
				Wave_Type[0] = 0;
				Wave_Type[1] = 1;
				Basic_Freq[0] = disting_freq * Index[1] / 1024;
				Basic_Freq[1] = disting_freq * Index[0] / 1024;
			}
		}
	}
	
	if(Basic_Freq[0]>160000)
	{
		Basic_Freq[0]+=625;
	}
	if(Basic_Freq[1]>160000)
	{
		Basic_Freq[1]+=625;
	}
}

void Max_SecondMax_Index(float Mag[], uint16_t index[], uint16_t ignore_DC, uint16_t len)
{
	uint16_t i, max_index, seconde_index;
	
    if(ignore_DC)
	{
		max_index = 1;
		seconde_index = 1;
		i = 2;
	}
	else
	{
		max_index = 0;
		seconde_index = 0;
		i = 1;
	}
	
	for(; i < len; i++)
	{
		if(Mag[i] > Mag[max_index])
		{
			seconde_index = max_index;
			max_index = i;
		}
		else if(Mag[i] > Mag[seconde_index])
		{
			seconde_index = i;
		}
	}
	
	index[0] = max_index;
	index[1] = seconde_index;
}










