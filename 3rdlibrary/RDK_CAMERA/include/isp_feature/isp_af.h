/***************************************************************************
 *                      COPYRIGHT NOTICE
 *             Copyright(C) 2024, D-Robotics Co., Ltd.
 *                     All rights reserved.
 ***************************************************************************/

#ifndef __HBN_ISP_AF_H__
#define __HBN_ISP_AF_H__

#ifdef __cplusplus
extern "C" {
#endif

#define HBN_ISP_AFM_BLOCK_POINT_NUM 4		/**< The number of AFM block point numbers */
#define HBN_ISP_AFM_LDG_Y_NUM 3			/**< The number of AFM Level Depend Gain in Y axis */
#define HBN_ISP_AFM_LDG_X_NUM 4			/**< The number of AFM Level Depend Gain in X axis */
#define HBN_ISP_AFM_CORING_COEFF_NUM 3		/**< The number of AFM coring coefficient */
#define HBN_ISP_AFM_IIR_FILTER_NUM 3		/**< The number of AFM IIR filter */
#define HBN_ISP_AFM_IIR_FILTER_COEFF_NUM 4	/**< The number of AFM IIR filter coefficient */
#define HBN_ISP_AFM_FIR_HIGHPASS_COEFF_NUM 2	/**< The number of AFM fir highpass filter coefficient*/
#define HBN_ISP_AFM_FIR_LOWPASS_COEFF_NUM 4	/**< The number of AFM fir lowpass filter coefficient */

typedef struct hbn_isp_afm_ldg_s {
	float iir_highpass_y[HBN_ISP_AFM_LDG_Y_NUM];	/**< IIR high pass on Y axis */
	uint8_t iir_highpass_x[HBN_ISP_AFM_LDG_X_NUM];	/**< IIR high pass on X axis */
	float iir_lowpass_y[HBN_ISP_AFM_LDG_Y_NUM];	/**< IIR low pass on Y axis */
	uint8_t iir_lowpass_x[HBN_ISP_AFM_LDG_X_NUM];	/**< IIR low pass on X axis */
	float fir_highpass_y[HBN_ISP_AFM_LDG_Y_NUM];	/**< FIR high pass on Y axis */
	uint8_t fir_highpass_x[HBN_ISP_AFM_LDG_X_NUM];	/**< FIR high pass on X axis */
	float fir_lowpass_y[HBN_ISP_AFM_LDG_Y_NUM];	/**< FIR low pass on Y axis */
	uint8_t fir_lowpass_x[HBN_ISP_AFM_LDG_X_NUM];	/**< FIR low pass on X axis */
} hbn_isp_afm_ldg_t;

typedef struct hbn_isp_afm_coring_s {
	uint8_t iir_highpass[HBN_ISP_AFM_CORING_COEFF_NUM];	/**< IIR high pass */
	uint8_t iir_lowpass[HBN_ISP_AFM_CORING_COEFF_NUM];	/**< IIR low pass */
	uint8_t fir_highpass[HBN_ISP_AFM_CORING_COEFF_NUM];	/**< FIR high pass */
	uint8_t fir_lowpass[HBN_ISP_AFM_CORING_COEFF_NUM];	/**< FIR low pass */
} hbn_isp_afm_coring_t;

typedef struct hbn_isp_afm_iir_s {
	uint8_t data_shift;	/**< Data shift */
	float highpass_weight;	/**< High pass weight */
	float lowpass_weight;	/**< Low pass weight */
	float highpass_coeff[HBN_ISP_AFM_IIR_FILTER_NUM][HBN_ISP_AFM_IIR_FILTER_COEFF_NUM];  /**< High pass coefficient */
	float lowpass_coeff[HBN_ISP_AFM_IIR_FILTER_NUM][HBN_ISP_AFM_IIR_FILTER_COEFF_NUM];   /**< Low pass coefficient */
} hbn_isp_afm_iir_t;

typedef struct hbn_isp_afm_fir_s {
	uint8_t data_shift;		/**< Data shift */
	float highpass_weight;		/**< High pass weight */
	float lowpass_weight;		/**< Low pass weight */
	uint8_t highpass_coeff[HBN_ISP_AFM_FIR_HIGHPASS_COEFF_NUM];	/**< High pass coefficient */
	uint8_t lowpass_coeff[HBN_ISP_AFM_FIR_LOWPASS_COEFF_NUM];	/**< Low pass coefficient */
} hbn_isp_afm_fir_t;

typedef struct hbn_isp_afm_attr_s {
	uint16_t bls;					/**< BLS components */
	uint8_t hist_threshold;				/**< Histogram threshold */
	float gamma;					/**< Gamma value */
	uint16_t hw_roi[HBN_ISP_AFM_BLOCK_POINT_NUM];	/**< Hardware ROI */
	hbn_isp_afm_ldg_t ldg;				/**< Level Dependent Gain configuration */
	hbn_isp_afm_coring_t coring;			/**< Coring configuration */
	hbn_isp_afm_iir_t iir;				/**< IIR configuration */
	hbn_isp_afm_fir_t fir;				/**< FIR configuration */
} hbn_isp_afm_attr_t;

#ifdef __cplusplus
}
#endif

#endif	//__HBN_ISP_AF_H__
