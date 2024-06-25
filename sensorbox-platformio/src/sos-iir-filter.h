/*
 * ESP32 Second-Order Sections IIR Filter implementation
 *
 * (c)2019 Ivan Kostoski
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *    
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdint.h>

struct SOS_Coefficients {
  float b1;
  float b2;
  float a1;
  float a2;
};

struct SOS_Delay_State {
  float w0 = 0;
  float w1 = 0;
};

extern "C" {
  int sos_filter_f32(float *input, float *output, int len, const SOS_Coefficients &coeffs, SOS_Delay_State &w);
} 
__asm__ (
  //
  // ESP32 implementation of IIR Second-Order Section filter 
  // Assumes a0 and b0 coefficients are one (1.0)
  //
  // float* a2 = input;
  // float* a3 = output;
  // int    a4 = len;
  // float* a5 = coeffs;
  // float* a6 = w; 
  // float  a7 = gain;
  //
  ".text                    \n"
  ".align  4                \n"
  ".global sos_filter_f32   \n"
  ".type   sos_filter_f32,@function\n"
  "sos_filter_f32:          \n"
  "  entry   a1, 16         \n"
  "  lsi     f0, a5, 0      \n" // float f0 = coeffs.b1;
  "  lsi     f1, a5, 4      \n" // float f1 = coeffs.b2;
  "  lsi     f2, a5, 8      \n" // float f2 = coeffs.a1;
  "  lsi     f3, a5, 12     \n" // float f3 = coeffs.a2;
  "  lsi     f4, a6, 0      \n" // float f4 = w[0];
  "  lsi     f5, a6, 4      \n" // float f5 = w[1];
  "  loopnez a4, 1f         \n" // for (; len>0; len--) { 
  "    lsip    f6, a2, 4    \n" //   float f6 = *input++;
  "    madd.s  f6, f2, f4   \n" //   f6 += f2 * f4; // coeffs.a1 * w0
  "    madd.s  f6, f3, f5   \n" //   f6 += f3 * f5; // coeffs.a2 * w1
  "    mov.s   f7, f6       \n" //   f7 = f6; // b0 assumed 1.0
  "    madd.s  f7, f0, f4   \n" //   f7 += f0 * f4; // coeffs.b1 * w0
  "    madd.s  f7, f1, f5   \n" //   f7 += f1 * f5; // coeffs.b2 * w1 -> result
  "    ssip    f7, a3, 4    \n" //   *output++ = f7;
  "    mov.s   f5, f4       \n" //   f5 = f4; // w1 = w0
  "    mov.s   f4, f6       \n" //   f4 = f6; // w0 = f6
  "  1:                     \n" // }
  "  ssi     f4, a6, 0      \n" // w[0] = f4;
  "  ssi     f5, a6, 4      \n" // w[1] = f5;
  "  movi.n   a2, 0         \n" // return 0;
  "  retw.n                 \n"
);

extern "C" {
  float sos_filter_sum_sqr_f32(float *input, float *output, int len, const SOS_Coefficients &coeffs, SOS_Delay_State &w, float gain);
}
__asm__ (
  //
  // ESP32 implementation of IIR Second-Order section filter with applied gain.
  // Assumes a0 and b0 coefficients are one (1.0)
  // Returns sum of squares of filtered samples
  //
  // float* a2 = input;
  // float* a3 = output;
  // int    a4 = len;
  // float* a5 = coeffs;
  // float* a6 = w;
  // float  a7 = gain;
  //
  ".text                    \n"
  ".align  4                \n"
  ".global sos_filter_sum_sqr_f32 \n"
  ".type   sos_filter_sum_sqr_f32,@function \n"
  "sos_filter_sum_sqr_f32:  \n"
  "  entry   a1, 16         \n" 
  "  lsi     f0, a5, 0      \n"  // float f0 = coeffs.b1;
  "  lsi     f1, a5, 4      \n"  // float f1 = coeffs.b2;
  "  lsi     f2, a5, 8      \n"  // float f2 = coeffs.a1;
  "  lsi     f3, a5, 12     \n"  // float f3 = coeffs.a2;
  "  lsi     f4, a6, 0      \n"  // float f4 = w[0];
  "  lsi     f5, a6, 4      \n"  // float f5 = w[1];
  "  wfr     f6, a7         \n"  // float f6 = gain;
  "  const.s f10, 0         \n"  // float sum_sqr = 0;
  "  loopnez a4, 1f         \n"  // for (; len>0; len--) {
  "    lsip    f7, a2, 4    \n"  //   float f7 = *input++;
  "    madd.s  f7, f2, f4   \n"  //   f7 += f2 * f4; // coeffs.a1 * w0
  "    madd.s  f7, f3, f5   \n"  //   f7 += f3 * f5; // coeffs.a2 * w1;
  "    mov.s   f8, f7       \n"  //   f8 = f7; // b0 assumed 1.0
  "    madd.s  f8, f0, f4   \n"  //   f8 += f0 * f4; // coeffs.b1 * w0;
  "    madd.s  f8, f1, f5   \n"  //   f8 += f1 * f5; // coeffs.b2 * w1; 
  "    mul.s   f9, f8, f6   \n"  //   f9 = f8 * f6;  // f8 * gain -> result
  "    ssip    f9, a3, 4    \n"  //   *output++ = f9;
  "    mov.s   f5, f4       \n"  //   f5 = f4; // w1 = w0
  "    mov.s   f4, f7       \n"  //   f4 = f7; // w0 = f7;
  "    madd.s  f10, f9, f9  \n"  //   f10 += f9 * f9; // sum_sqr += f9 * f9;
  "  1:                     \n"  // }
  "  ssi     f4, a6, 0      \n"  // w[0] = f4;
  "  ssi     f5, a6, 4      \n"  // w[1] = f5;
  "  rfr     a2, f10        \n"  // return sum_sqr; 
  "  retw.n                 \n"  // 
);


/**
 * Envelops above asm functions into C++ class
 */
struct SOS_IIR_Filter {

  const int num_sos;
  const float gain;
  SOS_Coefficients* sos = NULL;
  SOS_Delay_State* w = NULL;

  // Dynamic constructor
  SOS_IIR_Filter(size_t num_sos, const float gain, const SOS_Coefficients _sos[] = NULL): num_sos(num_sos), gain(gain) {
    if (num_sos > 0) {
      sos = new SOS_Coefficients[num_sos];
      if ((sos != NULL) && (_sos != NULL)) memcpy(sos, _sos, num_sos * sizeof(SOS_Coefficients));
      w = new SOS_Delay_State[num_sos]();
    }
  };

  // Template constructor for const filter declaration
  template <size_t Array_Size>
  SOS_IIR_Filter(const float gain, const SOS_Coefficients (&sos)[Array_Size]): SOS_IIR_Filter(Array_Size, gain, sos) {};

  /** 
   * Apply defined IIR Filter to input array of floats, write filtered values to output, 
   * and return sum of squares of all filtered values 
   */
  inline float filter(float* input, float* output, size_t len) {
    if ((num_sos < 1) || (sos == NULL) || (w == NULL)) return 0;
    float* source = input; 
    // Apply all but last Second-Order-Section 
    for(int i=0; i<(num_sos-1); i++) {                
      sos_filter_f32(source, output, len, sos[i], w[i]);      
      source = output;
    }      
    // Apply last SOS with gain and return the sum of squares of all samples  
    return sos_filter_sum_sqr_f32(source, output, len, sos[num_sos-1], w[num_sos-1], gain);
  }

  ~SOS_IIR_Filter() {
    if (w != NULL) delete[] w;
    if (sos != NULL) delete[] sos;
  }

};

//
// For testing only
//
struct No_IIR_Filter {  
  const int num_sos = 0;
  const float gain = 1.0;

  No_IIR_Filter() {};

  inline float filter(float* input, float* output, size_t len) {
    float sum_sqr = 0;
    float s;
    for(int i=0; i<len; i++) {
      s = input[i];
      sum_sqr += s * s;
    }
    if (input != output) {
      for(int i=0; i<len; i++) output[i] = input[i];
    }
    return sum_sqr;
  };
  
};

No_IIR_Filter None;


// DC-Blocker filter - removes DC component from I2S data
// See: https://www.dsprelated.com/freebooks/filters/DC_Blocker.html
// a1 = -0.9992 should heavily attenuate frequencies below 10Hz
SOS_Coefficients DC_BLOCKER_COEFFS[] = {{.b1 = -1.0f, .b2 = 0.0f, .a1 = 0.9992f, .a2 = 0.0f}};
SOS_IIR_Filter DC_BLOCKER = SOS_IIR_Filter(1.0f, DC_BLOCKER_COEFFS);

//
// Equalizer IIR filters to flatten microphone frequency response
// See respective .m file for filter design. Fs = 48Khz.
//
// Filters are represented as Second-Order Sections cascade with assumption
// that b0 and a0 are equal to 1.0 and 'gain' is applied at the last step
// B and A coefficients were transformed with GNU Octave:
// [sos, gain] = tf2sos(B, A)
// See: https://www.dsprelated.com/freebooks/filters/Series_Second_Order_Sections.html
// NOTE: SOS matrix 'a1' and 'a2' coefficients are negatives of tf2sos output
//

// TDK/InvenSense INMP441
// Datasheet: https://www.invensense.com/wp-content/uploads/2015/02/INMP441.pdf
// B ~= [1.00198, -1.99085, 0.98892]
// A ~= [1.0, -1.99518, 0.99518]

SOS_Coefficients INMP441_COEFFS[] = {{.b1 = -1.986920458344451, .b2 = +0.986963226946616, .a1 = +1.995178510504166, .a2 = -0.995184322194091}};
SOS_IIR_Filter INMP441 = SOS_IIR_Filter(1.00197834654696, INMP441_COEFFS);

//
// Weighting filters
//

//
// A-weighting IIR Filter, Fs = 48KHz
// (By Dr. Matt L., Source: https://dsp.stackexchange.com/a/36122)
// B = [0.169994948147430, 0.280415310498794, -1.120574766348363, 0.131562559965936, 0.974153561246036, -0.282740857326553, -0.152810756202003]
// A = [1.0, -2.12979364760736134, 0.42996125885751674, 1.62132698199721426, -0.96669962900852902, 0.00121015844426781, 0.04400300696788968]

SOS_Coefficients A_weighting_COEFFS[] = {
    {.b1 = -2.00026996133106, .b2 = +1.00027056142719, .a1 = -1.060868438509278, .a2 = -0.163987445885926},
    {.b1 = +4.35912384203144, .b2 = +3.09120265783884, .a1 = +1.208419926363593, .a2 = -0.273166998428332},
    {.b1 = -0.70930303489759, .b2 = -0.29071868393580, .a1 = +1.982242159753048, .a2 = -0.982298594928989}};
SOS_IIR_Filter A_weighting = SOS_IIR_Filter(0.169994948147430, A_weighting_COEFFS);

//
// C-weighting IIR Filter, Fs = 48KHz
// Designed by invfreqz curve-fitting, see respective .m file
// B = [-0.49164716933714026, 0.14844753846498662, 0.74117815661529129, -0.03281878334039314, -0.29709276192593875, -0.06442545322197900, -0.00364152725482682]
// A = [1.0, -1.0325358998928318, -0.9524000181023488, 0.8936404694728326   0.2256286147169398  -0.1499917107550188, 0.0156718181681081]

SOS_Coefficients C_weighting_COEFFS[] = {
    {.b1 = +1.4604385758204708, .b2 = +0.5275070373815286, .a1 = +1.9946144559930252, .a2 = -0.9946217070140883},
    {.b1 = +0.2376222404939509, .b2 = +0.0140411206016894, .a1 = -1.3396585608422749, .a2 = -0.4421457807694559},
    {.b1 = -2.0000000000000000, .b2 = +1.0000000000000000, .a1 = +0.3775800047420818, .a2 = -0.0356365756680430}};
SOS_IIR_Filter C_weighting = SOS_IIR_Filter(-0.491647169337140, C_weighting_COEFFS);
