#ifndef __CALCULATE_H
#define __CALCULATE_H
#include "main.h"


/*	误差系数		*/

#define k_Amp_1 2097152u  *  0.48f  /  3.3f  
#define	k_Amp_2 2097152u  *  0.43f  /  3.3f  
#define	k_Amp_3 2097152u  *  0.33f  /  3.3f  

uint32_t test_f(void);
void adjust_adc_sample_freq(uint32_t f);

/* 找出最小值位置 */
uint16_t Min_Float(float Mag[], uint16_t len);
uint16_t Min_Unsigned(uint16_t Mag[], uint16_t len);
/* 找出最大值位置 */
uint16_t Max_Unsigned(uint16_t Mag[], uint16_t len);

/* 在一定范围内找出最大值位置 */
uint16_t Max_Float_WithinRange(float Data[], uint16_t Left, uint16_t Right);

/* 通过FFT 计算各个频率分量幅值 */
void CalculateAmplitude_By_FFT(float *Am_Pointer, uint16_t *SampleData_Pointer);

/*	找众数		*/
uint16_t Most_Unsigned(uint16_t Mag[], uint16_t len);

/*	找最大值和第二大值索引	*/
void Max_SecondMax_Index(float *Mag, uint16_t index[], uint16_t ignore_DC, uint16_t len);

/*	判断波形及基频		*/
void Demultiplex(float Mag[], uint16_t Wave_Type[], uint32_t Basic_Freq[], uint16_t len, uint32_t disting_freq);


/*  一阶滞后滤波*/
//一般先均值
#define LPF_coff 0.4
#define LPxF_coff 0.6
#define LPF(_n1_,_n2_) (_n1_*LPF_coff+_n2_*LPxF_coff)
/*
for(i=0;i<...)
{
a[i]=LPF(a[i],a[i+1]);

}

*/


#endif
