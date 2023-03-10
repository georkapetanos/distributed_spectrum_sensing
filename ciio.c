#include <iio.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <fftw3.h>
#include "spdetect.h"
#include "mqtt.h"
#define USAGE_MSG "Usage: ciio [<arguments>]\n\n--help\t\tUsage message\n--freq\t\tTuner center frequency\n--rate\t\tSampling rate\n--uri\t\tDevice uri string\n-s </dev/ttyX>\tSet Serial device\n"
#define RECEIVER_RATE 16384
#define PLOT_HISTORY_AVERAGE 1000
#define DEVICE_URI "ip:172.16.0.1"
#define CUSENSE_SEGMENTS 8
#define DEFAULT_GAIN 45.0

FILE *gnuplot_pipe;
struct iio_device *phy;
struct iio_context *ctx;
struct iio_buffer *rxbuf;
bool plot = false;
bool sense = false;
bool cusense = false;
bool cuplot = false;

typedef struct iq_sample{
	int16_t i;
	int16_t q;
} iq_sampleT;

void ctrl_c_handler(int signal) {
	printf("pclose (gnuplot) = %d\n",pclose(gnuplot_pipe));
	//iio_buffer_destroy(rxbuf);
	//iio_context_destroy(ctx);
	exit(0);
}

void get_iq_amplitude(struct iq_sample *samples, double *samples_pd, int fft_bins, double sampling_rate) {
	int i;
	double SAMPLE_times_RECEIVER = ((double)sampling_rate * (double)fft_bins);

	for(i = 0; i < fft_bins; i++) {
		/*if(samples[i].i == 0 && samples[i].q == 0) {
			printf("get_iq_amplitude i = %d\n", i);
		}*/
		samples_pd[i] = 10*log10((samples[i].i * samples[i].i + samples[i].q * samples[i].q) / SAMPLE_times_RECEIVER);
	}
}

void average_power_time(double *samples_pd, double *averaged_samples_pd, int fft_bins, int hist) {
	int i;

	for(i = 0; i < fft_bins; i++) {
		/*avoid averaging faulty -inf values, you may adjust -200 lower noise floor*/
		if(samples_pd[i] < -200) {
			//printf("found %d %f\n", i, samples_pd[i]);
			continue;
		}
		averaged_samples_pd[i] = ((hist - 1) * averaged_samples_pd[i] / hist) + (samples_pd[i] / hist);
				/*if(averaged_samples_pd[i] < -100) {
			printf(" %d found: %f, %f\n", i, averaged_samples_pd[i], samples_pd[i]);
		}*/
	}
}

void fft_library(struct iq_sample * restrict samples_fd, struct iq_sample * restrict samples_td, int vector_size) {
	fftw_complex *in, *out;
	fftw_plan p;
	int i;
	
	in = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * vector_size);
	out = (fftw_complex *) fftw_malloc(sizeof(fftw_complex) * vector_size);
	
	for(i = 0; i < vector_size; i++) {
		in[i][0] = samples_td[i].q;
		in[i][1] = samples_td[i].i;
	}
	
	p = fftw_plan_dft_1d(vector_size, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
	fftw_execute(p);
	
	//save result and transpose vector
	for(i = 0; i < vector_size; i++) {
		samples_fd[vector_size - i - 1].q = out[i][0];
		samples_fd[vector_size - i - 1].i = out[i][1];
	}
	
	//printf("fft_library, i = %f, q = %f\n", out[16383][0], out[16383][0]);
	
	fftw_destroy_plan(p);
	fftw_free(in);
	fftw_free(out);
}

void fft(struct iq_sample * restrict samples_fd, struct iq_sample * restrict samples_td, int nof_samples) {
	int i, j;
	double sum_q, sum_i;
	double temp_cos, temp_sin, temp_phase;
	
	for(i = 0; i < nof_samples; i++) { // i = k
		sum_i = 0;
		sum_q = 0;
		for(j = 0; j < nof_samples; j++) { // j = n
			temp_phase = 2*M_PI*i*j/nof_samples;
			temp_cos = cos(temp_phase);
			temp_sin = (-sin(temp_phase));
			sum_q += samples_td[j].q * temp_cos + samples_td[j].i * temp_sin;
			sum_i += samples_td[j].q * temp_sin + samples_td[j].i * temp_cos;
		}
		samples_fd[i].q = sum_q;
		samples_fd[i].i = sum_i;
	}
}

void fft_shift(struct iq_sample *samples_fd, struct iq_sample *samples_fd_shifted, int nof_samples) {
	int i;
	//printf("fft_shift, i = %d, q = %d\n", samples_fd[0].i, samples_fd[0].q);
	for(i = 0; i < nof_samples / 2; i++) {
		samples_fd_shifted[(nof_samples / 2) + i] = samples_fd[i];
	}
	for(i = nof_samples / 2; i < nof_samples; i++) {
		samples_fd_shifted[i - (nof_samples / 2)] = samples_fd[i];
	}
	//printf("fft_shift, 2 i = %d, q = %d\n", samples_fd_shifted[8192].i, samples_fd_shifted[8192].q);
}

void plot_psd(double *samples_pd, int fft_bins, FILE *gnuplot, double sampling_rate) {
	int i;

	fprintf(gnuplot_pipe, "plot '-'\n");
	for (i = 0; i < fft_bins; i++) {
		fprintf(gnuplot, "%d %g\n", (int) (i * (sampling_rate / fft_bins) - sampling_rate / 2), samples_pd[i]);
	}
	fprintf(gnuplot, "e\n");
	fflush(gnuplot);
}

int receive(struct iio_context *ctx, double center_frequency, double sampling_rate, int plot_history_average) {
	struct iio_device *dev;
	struct iio_channel *rx0_i, *rx0_q;
	long long int sample_counter = 0;
	int16_t i_host, q_host;
	struct iq_sample *samples_td, *samples_fd, *samples_fd_shifted;
	double *samples_pd, *averaged_samples_pd, *concatenated_samples_pd = NULL;
	struct timespec  tv1, tv2;
	int i, j, k;
	
	//redirect output to /dev/null to avoid spamming terminal when exiting ciio
	gnuplot_pipe = popen("gnuplot -persist > /dev/null 2>&1", "w");
	fprintf(gnuplot_pipe, "set title \'Power Spectral Density\'\n");
	fprintf(gnuplot_pipe, "set terminal qt size 1280,720\n");
	fprintf(gnuplot_pipe, "set grid\n");
	fprintf(gnuplot_pipe, "set xlabel \'Frequency (Hz)\'\n");
	fprintf(gnuplot_pipe, "set ylabel \'Magnitude (dB)\'\n");
	fprintf(gnuplot_pipe, "set lt 1 lc 4\n");
	fprintf(gnuplot_pipe, "set yrange [-100:0]\n");
	if(cusense || cuplot) {
		fprintf(gnuplot_pipe, "set xrange [%d:%d]\n", (int) (-sampling_rate * CUSENSE_SEGMENTS / 2), (int) (sampling_rate * CUSENSE_SEGMENTS / 2));
	} else {
		fprintf(gnuplot_pipe, "set xrange [%d:%d]\n", (int) (-sampling_rate / 2), (int) (sampling_rate / 2));
	}
	
	samples_td = (iq_sampleT *) malloc(RECEIVER_RATE * sizeof(iq_sampleT));
	samples_fd = (iq_sampleT *) malloc(RECEIVER_RATE * sizeof(iq_sampleT));
	samples_fd_shifted = (iq_sampleT *) malloc(RECEIVER_RATE * sizeof(iq_sampleT));
	samples_pd = (double *) malloc(RECEIVER_RATE * sizeof(double));
	averaged_samples_pd = (double *) malloc(RECEIVER_RATE * sizeof(double));
	for(i = 0; i < RECEIVER_RATE; i++) {
		averaged_samples_pd[i] = -60;
	}
	i = 0;
	k = 0;
	
	if(cusense || cuplot) {
		concatenated_samples_pd = (double *) malloc(RECEIVER_RATE * CUSENSE_SEGMENTS * sizeof(double));
	}

	dev = iio_context_find_device(ctx, "cf-ad9361-lpc");

	rx0_i = iio_device_find_channel(dev, "voltage0", 0);
	rx0_q = iio_device_find_channel(dev, "voltage1", 0);

	iio_channel_enable(rx0_i);
	iio_channel_enable(rx0_q);

	rxbuf = iio_device_create_buffer(dev, RECEIVER_RATE, false);
	if (!rxbuf) {
		perror("Could not create RX buffer");
		//shutdown();
	}

	signal(SIGINT, ctrl_c_handler);

	while (true) {
		void *p_dat, *p_end, *t_dat;
		ptrdiff_t p_inc;

		iio_buffer_refill(rxbuf);

		p_inc = iio_buffer_step(rxbuf);
		p_end = iio_buffer_end(rxbuf);
		for (p_dat = iio_buffer_first(rxbuf, rx0_i); p_dat < p_end; p_dat += p_inc, t_dat += p_inc) {
			const int16_t i = ((int16_t*)p_dat)[0]; // Real (I)
			const int16_t q = ((int16_t*)p_dat)[1]; // Imag (Q)
			//Convert the sample from hardware format to host format. 
			iio_channel_convert(rx0_i, &i_host, &i);
			iio_channel_convert(rx0_q, &q_host, &q);
			samples_td[sample_counter].i = i_host;
			samples_td[sample_counter].q = q_host;
			sample_counter++;
		}
		clock_gettime(CLOCK_MONOTONIC_RAW, &tv1);
		fft_library(samples_fd, samples_td, RECEIVER_RATE);
		fft_shift(samples_fd, samples_fd_shifted, RECEIVER_RATE);
		get_iq_amplitude(samples_fd_shifted, samples_pd, RECEIVER_RATE, sampling_rate);
		average_power_time(samples_pd, averaged_samples_pd, RECEIVER_RATE, plot_history_average);
		if(plot) {
			plot_psd(averaged_samples_pd, RECEIVER_RATE, gnuplot_pipe, sampling_rate);
		} else if(sense) {
			i++;
			if(i == 6000) {
				i = 0;
				publish_station_info(center_frequency, sampling_rate);
				spectrum_monitor(averaged_samples_pd, RECEIVER_RATE, sampling_rate / RECEIVER_RATE, sampling_rate, gnuplot_pipe, 0);
			}
		} else if(cusense || cuplot) {
			i++;
			if(i == 100) {
				i = 0;
				for(j = 0; j < RECEIVER_RATE; j++) {
					concatenated_samples_pd[k * RECEIVER_RATE + j] = averaged_samples_pd[j];
				}
				k++;
				if(k == CUSENSE_SEGMENTS) {
					k = 0;
					if(cuplot) {
						plot_psd(concatenated_samples_pd, RECEIVER_RATE * CUSENSE_SEGMENTS, gnuplot_pipe, sampling_rate * CUSENSE_SEGMENTS);
					} else if(cusense) {
						publish_station_info(center_frequency + (sampling_rate * CUSENSE_SEGMENTS / 2) - (sampling_rate / 2), sampling_rate * CUSENSE_SEGMENTS);
						spectrum_monitor(concatenated_samples_pd, RECEIVER_RATE * CUSENSE_SEGMENTS, sampling_rate * CUSENSE_SEGMENTS / RECEIVER_RATE * CUSENSE_SEGMENTS, sampling_rate * CUSENSE_SEGMENTS, gnuplot_pipe, 1);
					}
					printf("Changing center frequency to %lf\n", center_frequency);
					iio_channel_attr_write_longlong(iio_device_find_channel(phy, "altvoltage0", true), "frequency", center_frequency);
						for(j = 0; j < RECEIVER_RATE; j++) {
							averaged_samples_pd[j] = -60;
						}
				} else {
					printf("Changing center frequency to %lf\n", center_frequency + k * sampling_rate);
					iio_channel_attr_write_longlong(iio_device_find_channel(phy, "altvoltage0", true), "frequency", center_frequency + k * sampling_rate);
				}
			}
		}
		
		clock_gettime(CLOCK_MONOTONIC_RAW, &tv2);
		/*printf ("Total CPU time = %.5g seconds.\n",
			(double) (tv2.tv_nsec - tv1.tv_nsec) / 1000000000.0 +
			(double) (tv2.tv_sec - tv1.tv_sec));*/
		sample_counter = 0;
	}

	free(samples_td);
	free(samples_fd);
	free(samples_fd_shifted);
	free(samples_pd);
	free(averaged_samples_pd);

	iio_buffer_destroy(rxbuf);
}

int main(int argc, char **argv) {
	int i, plot_history_average = 0;
	double center_frequency = 0, sampling_rate = 0, gain = DEFAULT_GAIN;
	char user_device_uri[256];
	
	strcpy(user_device_uri, DEVICE_URI);
	
	if(argc < 2) {
		printf("Usage: ./ciio --help\n");
		return 0;
	}
	for(i = 1; i < argc; i++) {
		if((strncmp(argv[i], "--freq", 6) == 0) && (argc - 1 > i)) {
			center_frequency = atof(argv[i + 1]);
			i++;
		} else if((strncmp(argv[i], "--rate", 6) == 0) && (argc - 1 > i)) {
			sampling_rate = atof(argv[i + 1]);
			//printf("rate=%f\n", sampling_rate);
			i++;
		} else if(strncmp(argv[i], "--plot", 6) == 0) {
			plot = true;
			plot_history_average = 10;
		} else if(strncmp(argv[i], "--sense", 7) == 0) {
			plot_history_average = 1000;
			mqtt_setup();
			sense = true;
		} else if(strncmp(argv[i], "--cusense", 9) == 0) {
			plot_history_average = 10;
			cusense = true;
			mqtt_setup();
		} else if(strncmp(argv[i], "--cuplot", 8) == 0) {
			plot_history_average = 10;
			cuplot = true;
		} else if((strncmp(argv[i], "--gain", 6) == 0) && (argc - 1 > i)) {
			gain = atof(argv[i + 1]);
			i++;
		} else if((strncmp(argv[i], "--uri", 5) == 0) && (argc - 1 > i)) {
			strcpy(user_device_uri, argv[i + 1]);
			i++;
		} else if(strncmp(argv[i], "--help", 6) == 0) {
			printf("%s\n", USAGE_MSG);
			return 0;
		} else {
			printf("Invalid Syntax\n");
			return 0;
		}
	}
	
	ctx = iio_create_context_from_uri(user_device_uri);
	printf("ctx = %p\n", ctx);
	if(ctx == NULL) {
		printf("Error opening device at %s\n", user_device_uri);
		return 0;
	}

	phy = iio_context_find_device(ctx, "ad9361-phy");
	printf("phy = %p\n", phy);

	iio_channel_attr_write_longlong(
		iio_device_find_channel(phy, "altvoltage0", true),
		"frequency",
		center_frequency);

	iio_channel_attr_write_longlong(
		iio_device_find_channel(phy, "voltage0", false),
		"sampling_frequency",
		sampling_rate);
	iio_channel_attr_write_raw(
		iio_device_find_channel(phy, "voltage0", false),
		"gain_control_mode",
		"manual", 7);
	iio_channel_attr_write_longlong(
		iio_device_find_channel(phy, "voltage0", false),
		"hardwaregain",
		gain);
	receive(ctx, center_frequency, sampling_rate, plot_history_average);

	iio_context_destroy(ctx);

	return 0;
} 
