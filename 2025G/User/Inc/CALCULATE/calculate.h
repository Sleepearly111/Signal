#ifndef __CALCULATE_H
#define __CALCULATE_H
#include "main.h"


/*	���ϵ��		*/

#define k_Amp_1 2097152u  *  0.48f  /  3.3f  
#define	k_Amp_2 2097152u  *  0.43f  /  3.3f  
#define	k_Amp_3 2097152u  *  0.33f  /  3.3f  

uint32_t test_f(void);
void adjust_adc_sample_freq(uint32_t f);

/* �ҳ���Сֵλ�� */
uint16_t Min_Float(float Mag[], uint16_t len);
uint16_t Min_Unsigned(uint16_t Mag[], uint16_t len);
/* �ҳ����ֵλ�� */
uint16_t Max_Unsigned(uint16_t Mag[], uint16_t len);

/* ��һ����Χ���ҳ����ֵλ�� */
uint16_t Max_Float_WithinRange(float Data[], uint16_t Left, uint16_t Right);

/* ͨ��FFT �������Ƶ�ʷ�����ֵ */
void CalculateAmplitude_By_FFT(float *Am_Pointer, uint16_t *SampleData_Pointer);

/*	������		*/
uint16_t Most_Unsigned(uint16_t Mag[], uint16_t len);

/*	�����ֵ�͵ڶ���ֵ����	*/
void Max_SecondMax_Index(float *Mag, uint16_t index[], uint16_t ignore_DC, uint16_t len);

/*	�жϲ��μ���Ƶ		*/
void Demultiplex(float Mag[], uint16_t Wave_Type[], uint32_t Basic_Freq[], uint16_t len, uint32_t disting_freq);


/*  һ���ͺ��˲�*/
//һ���Ⱦ�ֵ
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


