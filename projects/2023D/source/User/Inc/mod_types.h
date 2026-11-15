#ifndef MOD_TYPES_H
#define MOD_TYPES_H

/* 调制类型 (Classify 的输出, 全系统公用) */
typedef enum {
  MOD_UNKNOWN = 0,
  MOD_AM,
  MOD_FM,
  MOD_CW,
  MOD_2ASK,
  MOD_2FSK,
  MOD_2PSK
} ModType_t;

/* 参数估计结果 (Estimate 填充, HMI 显示) */
typedef struct {
  ModType_t type;
  float ma;       /* AM 调幅系数 */
  float F_hz;     /* AM/FM 调制频率 (已吸附到 1..5kHz 候选) */
  float mf;       /* FM 调频系数 */
  float dfmax;    /* FM 最大频偏 Hz */
  int   Rc_bps;   /* 键控码速率 (已吸附到 6000/8000/10000) */
  float h;        /* 2FSK 移频键控系数 */
} EstResult_t;

#endif /* MOD_TYPES_H */
