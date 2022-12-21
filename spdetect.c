#include "spdetect.h"

void spectrum_monitor(double *samples, int vector_size, double fft_bin_size) {

}

double average_power(double *samples, int vector_size) {
	int i;
	double average_power_reduct = 0;
	
	for(i = 0; i < vector_size; i++) {
		average_power_reduct += samples[i];
	}
	
	return (average_power_reduct /= vector_size);
}
