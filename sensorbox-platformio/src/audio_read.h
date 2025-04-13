
/*
 * Sound level measured by I2S Microphone and send by MQTT
 * can be used for home automation like openHAB
 *
 * Based on the great work from  2019 Ivan Kostoski
 * https://github.com/ikostoski/esp32-i2s-slm
 *
 */

#pragma once

#include <my_config.h>

#ifdef THE_BOX

#include <driver/i2s_std.h>
#include "esp_dsp.h"
#include "sos-iir-filter.h"
#include "freertos/semphr.h"

#define TAG_AUDIO "audio_read"

//
// Sampling
//
#define SAMPLE_RATE 48000 // Hz, fixed to design of IIR filters
#define SAMPLE_BITS 32    // bits
#define SAMPLE_T int32_t
#define NUM_SAMPLES_SHORT 4
#define SAMPLES_SHORT (SAMPLE_RATE / NUM_SAMPLES_SHORT) // ~125ms * 2
#define SAMPLES_LEQ (SAMPLE_RATE * LEQ_PERIOD)
#define AUDIO_SAMPLES_PER_DMA_BUFFER 1024
#define AUDIO_DMA_BUFFER_COUNT 3
#define DMA_BANK_SIZE (SAMPLES_SHORT / 16)
#define DMA_BANKS 32
#define FFT_N 2048
#define FFT_MOD_SIZE (FFT_N / 2 + 1) // Number of samples for modification, ie up to Nyquist

#define LEQ_PERIOD 1 // second(s)
// #define WEIGHTING A_weighting // Also avaliable: 'C_weighting' or 'None' (Z_weighting)
#define LEQ_UNITS "LAeq" // customize based on above weighting used
#define DB_UNITS "dBA"   // customize based on above weighting used

// NOTE: Some microphones require at least DC-Blocker filter
#define MIC_EQUALIZER INMP441 // See below for defined IIR filters or set to 'None' to disable
#define MIC_OFFSET_DB 3.0103  // Default offset (sine-wave RMS vs. dBFS). Modify this value for linear calibration

// Customize these values from microphone datasheet
#define MIC_SENSITIVITY -26   // dBFS value expected at MIC_REF_DB (Sensitivity value from datasheet)
#define MIC_REF_DB 94.0       // Value at which point sensitivity is specified in datasheet (dB)
#define MIC_OVERLOAD_DB 116.0 // dB - Acoustic overload point
#define MIC_NOISE_DB 29       // dB - Noise floor
#define MIC_BITS 16           // valid number of bits in I2S data
// #define MIC_BITS 24           // valid number of bits in I2S data
#define MIC_CONVERT(s) (s >> (SAMPLE_BITS - MIC_BITS))
#define I2S_TASK_PRI 4
#define I2S_TASK_STACK 1024 + 2048

// Data we push to 'samples_queue'
struct sum_queue_t
{
    // Sum of squares of mic samples, after Equalizer filter
    float sum_sqr_SPL;
    // Sum of squares of weighted mic samples
    float sum_sqr_weighted;
    // Debug only, FreeRTOS ticks we spent processing the I2S data
    uint32_t proc_ticks;
};
QueueHandle_t samples_queue;
SemaphoreHandle_t fft_calculated_samaphore;
double MIC_REF_AMPL;
SOS_IIR_Filter currentWeightingFilter = A_weighting;

i2s_chan_handle_t rx_handle;

bool i2s_inited = false;
bool fft_inited = false;
bool mic_i2s_reader_task_read = false;
TaskHandle_t mic_i2s_reader_handle;
// Static buffer for block of samples
float samples[SAMPLES_SHORT] __attribute__((aligned(4)));

void do_fft_and_log_resample(float *samples, uint8_t *spectrum_log);

// I2S Microphone sampling setup
//
esp_err_t mic_i2s_init()
{
    // Setup I2S to sample mono channel for SAMPLE_RATE * SAMPLE_BITS

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_DMA_BUFFER_COUNT,
        .dma_frame_num = AUDIO_SAMPLES_PER_DMA_BUFFER, // Maybe make this 2 since we are using a callback.
        .auto_clear = false,
    };

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = (i2s_data_bit_width_t)MIC_BITS,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_LEFT,
            .ws_width = 32,
            .ws_pol = false,
            .bit_shift = true,
            .msb_right = false,
            // .left_align = true,
            // .big_endian = false,
            // .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_SCK_PIN,
            .ws = (gpio_num_t)I2S_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = (gpio_num_t)I2S_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    //  Allocate a new RX channel and get the handle of this channel
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    // Initialize the channel
    ret |= i2s_channel_init_std_mode(rx_handle, &std_cfg);

    // Before reading data, start the RX channel first
    ret |= i2s_channel_enable(rx_handle);

    if (ret == ESP_OK)
    {
        i2s_inited = true;
    }

    return ret;
}

esp_err_t mic_i2s_deinit()
{
    esp_err_t ret = i2s_channel_disable(rx_handle);
    ret |= i2s_del_channel(rx_handle);
    i2s_inited = false;

    return ret;
}

//
// I2S Reader Task
//
// Rationale for separate task reading I2S is that IIR filter
// processing cam be scheduled to different core on the ESP32
//
//
// As this is intended to run as separate hihg-priority task,
// we only do the minimum required work with the I2S data
// until it is 'compressed' into sum of squares
//
// FreeRTOS priority and stack size (in 32-bit words)

void mic_i2s_reader_task(void *parameter)
{
    size_t bytes_read = 0;

    if (!i2s_inited)
    {
        esp_err_t ret = mic_i2s_init();

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG_AUDIO, "Couldn't initialize I2S. Error = %i", ret);
            vTaskDelete(NULL);
        }

        // Discard first few bytes, microphone may have startup time (i.e. INMP441 up to 83ms)
        ret |= i2s_channel_read(rx_handle, &samples, SAMPLES_SHORT * sizeof(SAMPLE_T), &bytes_read, portMAX_DELAY);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG_AUDIO, "Couldn't read I2S. Error = %i", ret);
            mic_i2s_deinit();
            vTaskDelete(NULL);
        }
    }

    for (int read_count = 0; read_count < NUM_SAMPLES_SHORT && mic_i2s_reader_task_read; read_count++)
    {
        // Block and wait for microphone values from I2S
        //
        // Data is moved from DMA buffers to our 'samples' buffer by the driver ISR
        // and when there is requested ammount of data, task is unblocked
        //
        // Note: i2s_read does not care it is writing in float[] buffer, it will write
        //       integer values to the given address, as received from the hardware peripheral.

        i2s_channel_read(rx_handle, &samples, SAMPLES_SHORT * sizeof(SAMPLE_T), &bytes_read, portMAX_DELAY);

        if (bytes_read != SAMPLES_SHORT * sizeof(SAMPLE_T))
        {
            ESP_LOGE(TAG_AUDIO, "Short read from I2S. Error = %i", bytes_read);
            break;
        }

        if (!mic_i2s_reader_task_read)
            break;

        TickType_t start_tick = xTaskGetTickCount();

        // Convert (including shifting) integer microphone values to floats,
        // using the same buffer (assumed sample size is same as size of float),
        // to save a bit of memory
        SAMPLE_T *int_samples = (SAMPLE_T *)&samples;

        // for (int i = 0; i < 106; i++)
        //     Serial.printf("%ld ", int_samples[i]);
        // Serial.println();

        for (int i = 0; i < SAMPLES_SHORT; i++)
            samples[i] = MIC_CONVERT(int_samples[i]);
        sum_queue_t q;

        // DC_BLOCKER.filter(samples, samples, SAMPLES_SHORT);

        // Apply equalization and calculate Z-weighted sum of squares,
        // writes filtered samples back to the same buffer.
        q.sum_sqr_SPL = MIC_EQUALIZER.filter(samples, samples, SAMPLES_SHORT);

        // DC_BLOCKER.filter(samples, samples, SAMPLES_SHORT);

        // Apply weighting and calucate weigthed sum of squares
        q.sum_sqr_weighted = currentWeightingFilter.filter(samples, samples, SAMPLES_SHORT);

        // Debug only. Ticks we spent filtering and summing block of I2S data
        q.proc_ticks = xTaskGetTickCount() - start_tick;

        // Send the sums to FreeRTOS queue where main task will pick them up
        // and further calcualte decibel values (division, logarithms, etc...)
        xQueueSend(samples_queue, &q, portMAX_DELAY);
    }

    do_fft_and_log_resample(samples, (uint8_t *)parameter);
    xSemaphoreGive(fft_calculated_samaphore);

    if (i2s_inited)
    {
        mic_i2s_deinit();
    }

    vTaskDelete(NULL);
}

void fill_fft_samples(float *repurposed_samples, float *window)
{
    // Start from the end of the array to avoid overwriting values that are yet to be processed
    for (int i = FFT_N - 1; i >= 0; i--)
    {
        repurposed_samples[i * 2 + 0] = repurposed_samples[i] * window[i];
        repurposed_samples[i * 2 + 1] = 0;
    }
}

void log_resample_fft(float *fft, uint8_t *resampled_fft)
{
    int i, j, bin_start_index, bin_end_index, bin_count;
    int prev_bin_start = -1;
    int unique_count = 0;
    float *log_bins = &samples[FFT_N + 1]; // repurposing the samples array
    float m;

    // Calculate the center frequencies of the logarithmic bins
    for (i = 0;; i++)
    {
        log_bins[i] = ((float)SAMPLE_RATE * 2 / FFT_N) * powf(2.0f, (float)i / 12);

        if (log_bins[i] > (SAMPLE_RATE / 2))
        {
            // I am not considering the last bin, to make the number aligned and a multiple of 4, so subtract 1
            // the answer turns out to be 84 bins at 48000 Hz and 2048 FFT size
            bin_count = i - 1;
            break;
        }
    }

    // // Map each logarithmic bin to a set of linearly spaced bins and take max of their values
    // for (i = 0; i < bin_count - 1; i++)
    // {
    //     bin_start_index = log_bins[i] / ((float)SAMPLE_RATE / (float)FFT_N);
    //     bin_end_index = log_bins[i + 1] / ((float)SAMPLE_RATE / (float)FFT_N);

    //     // sum = 0;
    //     // for (j = bin_start_index; j <= bin_end_index; j++)
    //     //     sum += fft[j];
    //     // resampled_fft[i] = sum / (float)(bin_end_index - bin_start_index + 1);
    //     m = 0;
    //     for (j = bin_start_index; j <= bin_end_index; j++)
    //         m = max(m, fft[j]);

    //     if (m > 0)
    //         // arbritarily scale by 2 for a bit more resolution in the display
    //         resampled_fft[i] = 2 * static_cast<uint8_t>(min(255.0f, m));
    //     else
    //         resampled_fft[i] = 0;

    //     printf("%d (%df - %df): %fHz %d\n", i, bin_start_index, bin_end_index, log_bins[i], resampled_fft[i]);
    // }

    // For each logarithmic bin (except the last), compute the FFT value.
    // Only store the value if the computed bin_start differs from the last.
    for (i = 0; i < bin_count - 1; i++)
    {
        bin_start_index = log_bins[i] / ((float)SAMPLE_RATE / (float)FFT_N);
        bin_end_index = log_bins[i + 1] / ((float)SAMPLE_RATE / (float)FFT_N);

        while (bin_start_index == bin_end_index)
        {
            bin_end_index = log_bins[++i + 1] / ((float)SAMPLE_RATE / (float)FFT_N);
        }

        m = 0;
        for (j = bin_start_index; j <= bin_end_index; j++)
            m = fmax(m, fft[j]);

        // Only store if key differs from previous.
        if (bin_start_index != prev_bin_start)
        {
            // arbitrarily scale by 2 for a bit more resolution in the display
            resampled_fft[unique_count++] = static_cast<uint8_t>(min(255.0f, 2 * m));
            prev_bin_start = bin_start_index;
        }
    }
}

void do_fft(float *y_cf)
{
    int N = FFT_N;
    float max_mag = -INFINITY;
    float fundamental_freq = 0;

    // Pointers to result arrays
    float *y1_cf = &y_cf[0];
    // float *y2_cf = &y_cf[FFT_N];

    if (!fft_inited)
    {
        esp_err_t ret = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG_AUDIO, "Couldn't initialize FFT. Error = %i", ret);
            return;
        }

        fft_inited = true;
    }

    dsps_fft2r_fc32(y_cf, N);
    // Bit reverse
    dsps_bit_rev_fc32(y_cf, N);
    // Convert one complex vector to two complex vectors
    dsps_cplx2reC_fc32(y_cf, N);

    for (int i = 0; i < N / 2; i++)
    {
        y1_cf[i] = 10 * log10f((y1_cf[i * 2 + 0] * y1_cf[i * 2 + 0] + y1_cf[i * 2 + 1] * y1_cf[i * 2 + 1]) / N);
        // y1_cf[i] = MIC_OFFSET_DB + MIC_REF_DB + 20 * log10f(y1_cf[i] / MIC_REF_AMPL);

        if (y1_cf[i] > max_mag)
        {
            max_mag = y1_cf[i];
            fundamental_freq = i * float(SAMPLE_RATE) / N;
        }

        // y2_cf[i] = 10 * log10f((y2_cf[i * 2 + 0] * y2_cf[i * 2 + 0] + y2_cf[i * 2 + 1] * y2_cf[i * 2 + 1]) / N);
    }

    ESP_LOGW(TAG_AUDIO, "Fundamental freq: %f Mag: %f", fundamental_freq, max_mag);

    // ESP_LOGW(TAG_AUDIO, "\nSignal y1_cf");
    // dsps_view(y1_cf, N / 2, 64, 10, -30, 90, '|');

    // ESP_LOGW(TAG_AUDIO, "\nResampled fft");
    // dsps_view(spectrum_log, FFT_MOD_SIZE, 64, 10, -60, 255, '|');
}

void do_fft_and_log_resample(float *samples, uint8_t *spectrum_log)
{
    // Window coefficients
    float *wind = &samples[FFT_N * 3];
    // dsps_wind_hann_f32(wind, FFT_N);
    dsps_wind_blackman_harris_f32(wind, FFT_N);
    fill_fft_samples(samples, wind);
    do_fft(samples);
    log_resample_fft(samples, spectrum_log);
}

//
// Note: Use doubles, not floats, here unless you want to pin
//       the task to whichever core it happens to run on at the moment
//
void audio_read(float *dbA, float *dbZ, u_int8_t *fft_resampled)
{
    mic_i2s_reader_task_read = true;
    // Create FreeRTOS queue
    samples_queue = xQueueCreate(NUM_SAMPLES_SHORT, sizeof(sum_queue_t));
    fft_calculated_samaphore = xSemaphoreCreateBinary();
    MIC_REF_AMPL = pow10(double(MIC_SENSITIVITY) / 20) * ((1 << (MIC_BITS - 1)) - 1);

    // Create the I2S reader FreeRTOS task
    // NOTE: Current version of ESP-IDF will pin the task
    //       automatically to the first core it happens to run on
    //       (due to using the hardware FPU instructions).
    //       For manual control see: xTaskCreatePinnedToCore
    xTaskCreate(mic_i2s_reader_task, "Mic I2S Reader", I2S_TASK_STACK, fft_resampled, I2S_TASK_PRI, &mic_i2s_reader_handle);

    sum_queue_t q;
    uint32_t Leq_samples = 0;
    double Leq_sum_sqr = 0;
    double Leq_sum_sqr_unweighted = 0;

    // Read sum of samaples, calculated by 'i2s_reader_task'
    while (xQueueReceive(samples_queue, &q, 1000 / portTICK_PERIOD_MS))
    {
        if (!i2s_inited)
        {
            mic_i2s_reader_task_read = false;
            break;
        }
        // Calculate dB values relative to MIC_REF_AMPL and adjust for microphone reference
        double short_RMS = sqrt(double(q.sum_sqr_SPL) / SAMPLES_SHORT);
        double short_SPL_dB = MIC_OFFSET_DB + MIC_REF_DB + 20 * log10(short_RMS / MIC_REF_AMPL);

        // In case of acoustic overload or below noise floor measurement, report infinty Leq value
        if (short_SPL_dB > MIC_OVERLOAD_DB || isnan(short_SPL_dB) || short_SPL_dB < MIC_NOISE_DB)
        {
            mic_i2s_reader_task_read = false;
            break;
        }

        // Accumulate Leq sum
        Leq_sum_sqr += q.sum_sqr_weighted;
        Leq_sum_sqr_unweighted += q.sum_sqr_SPL;
        Leq_samples += SAMPLES_SHORT;

        // When we gather enough samples, calculate new Leq value
        if (Leq_samples >= SAMPLE_RATE * LEQ_PERIOD)
        {
            mic_i2s_reader_task_read = false;
            // Leq_sum_sqr = 0;
            // Leq_samples = 0;

            *dbA = MIC_OFFSET_DB + MIC_REF_DB + 20 * log10(sqrt(Leq_sum_sqr / Leq_samples) / MIC_REF_AMPL);
            *dbZ = MIC_OFFSET_DB + MIC_REF_DB + 20 * log10(sqrt(Leq_sum_sqr_unweighted / Leq_samples) / MIC_REF_AMPL);

            ESP_LOGW(TAG_AUDIO, "Leq: %f dbA, %f dbZ", *dbA, *dbZ);

            // waiting for fft
            xSemaphoreTake(fft_calculated_samaphore, 1000 / portTICK_PERIOD_MS);

            return;
        }
    }

    ESP_LOGE(TAG_AUDIO, "Queue receive failed");
    mic_i2s_reader_task_read = false;
}

#endif