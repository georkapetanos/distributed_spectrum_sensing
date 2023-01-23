#include <stdio.h>
#include <unistd.h>
#include "spdetect.h"
#include "mqtt.h"
#define AVG_STEP 16
#define MIN_SNR 1

void spectrum_monitor(double *samples, int vector_size, double fft_bin_size, double sampling_rate, FILE *gnuplot, int cumulative) {
	double avg_power = average_power(samples, vector_size);
	double avg_local, possible_signal_power = 0;
	int i, j, k, start_freq = -1, end_freq = -1;
	char payload[128];
	
	printf("Average power = %f\n", avg_power);
	fprintf(gnuplot, "plot '-'\n");
	for(i = 0; i < vector_size; i+=AVG_STEP) {
		avg_local = 0;
		for(j = 0; j < AVG_STEP; j++) {
			avg_local += samples[i+j];
		}
		avg_local /= AVG_STEP;
		
		if(avg_local - avg_power > MIN_SNR) {
			//possible signal
			if(start_freq == -1) {
				start_freq = i;
			} else {
				end_freq = i;
				for(k = start_freq + AVG_STEP / 2; k < end_freq + AVG_STEP / 2; k++) {
					possible_signal_power += samples[k];
				}
				possible_signal_power /= (end_freq - start_freq);
			}
			
		} else {
			if(end_freq != -1) {
				//end possible signal here
				printf("Rx @ %10.3e to %10.3e, ", (start_freq * fft_bin_size - sampling_rate / 2), (end_freq * fft_bin_size - sampling_rate / 2));
				//if(cumulative == 0) {
					fprintf(gnuplot, "%d %g\n", (int) (start_freq * fft_bin_size - sampling_rate / 2), possible_signal_power);
					fprintf(gnuplot, "%d %g\n", (int) (end_freq * fft_bin_size - sampling_rate / 2), possible_signal_power);
					//} else if(cumulative == 1) {
				
				//}
				sprintf(payload, "%10.3e,%10.3e,%6g", (double) (start_freq * fft_bin_size - sampling_rate / 2), (double) (end_freq * fft_bin_size - sampling_rate / 2), possible_signal_power);
				publish_message(payload, 28, 1);
				start_freq = -1;
				end_freq = -1;
				possible_signal_power = 0;
			}
		}
	}
	fprintf(gnuplot, "e\n");
	fflush(gnuplot);
	printf("\n");
}

double average_power(double *samples, int vector_size) {
	int i;
	double average_power_reduct = 0;
	
	for(i = 0; i < vector_size; i++) {
		average_power_reduct += samples[i];
	}

	return (average_power_reduct /= vector_size);
}
