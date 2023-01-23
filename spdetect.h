#ifndef SP_DETECT_H
#define SP_DETECT_H

//receive detection struct
typedef struct rx_dtc{
    double center_frequency;
    double bandwidth;
    double max_snr;
} RX_DTC;

void spectrum_monitor(double *samples, int vector_size, double fft_bin_size, double sampling_rate, FILE *gnuplot, int cumulative);
double average_power(double *samples, int vector_size);

#endif
