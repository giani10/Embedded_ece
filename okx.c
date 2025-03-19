#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <libwebsockets.h>
#include <jansson.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

// --------------------- Color Macros ---------------------
#define KGRN "\033[0;32m"    // Green
#define KCYN "\033[0;36m"    // Cyan
#define KRED "\033[0;31m"    // Red
#define KYEL "\033[1;33m"    // Bright Yellow
#define KBLU "\033[0;34m"    // Blue
#define KCYN_L "\033[1;36m"  // Bright Cyan
#define RESET "\033[0m"      // Reset

// --------------------- Configuration Constants ---------------------
#define TRADE_BUFFER_SIZE 100000  // Maximum trades stored per symbol (15-minute window)
#define MA_HISTORY_SIZE 8         // Number of moving average records (one per minute)
#define FIFTEEN_MINUTES (15 * 60)
#define MAX_INSTRUMENTS 8         // Exactly the required 8 symbols

// --------------------- Global Log Files ---------------------
FILE *timing_file = NULL;    // Logs scheduled vs. actual start time differences
FILE *cpu_idle_file = NULL;  // Logs CPU idle percentage

// --------------------- Data Structures ---------------------

// Trade structure with high-resolution timestamp (in seconds).
typedef struct {
    double timestamp;
    double price;
    double volume;
    double delay;
} trade_t;

// Moving average record computed every minute.
typedef struct {
    double timestamp;           // Computation time
    double moving_avg;          // Average price over last 15 minutes
    double total_volume;        // Total volume over last 15 minutes
    double avg_delay;           // Average processing delay for trades
    double avg_scheduled_delay; // Average scheduled delay for trades
} ma_entry_t;

// Instrument data structure.
typedef struct {
    char instrument[16];
    trade_t trades[TRADE_BUFFER_SIZE];
    int trade_count;
    ma_entry_t ma_history[MA_HISTORY_SIZE];
    int ma_count;
    double max_corr;            // Maximum Pearson correlation (from MA vectors)
    char max_corr_symbol[16];   // Symbol achieving maximum correlation
    double max_corr_time;       // Timestamp (current minute) when max correlation computed
    double max_corr_ma_time;    // Timestamp of the MA vector that resulted in max correlation
    FILE *trans_file;           // Transactions log file
    FILE *ma_file;              // Moving average log file
    FILE *corr_file;            // Correlation log file
} moving_avg_t;

static moving_avg_t instruments[MAX_INSTRUMENTS];
static int num_instruments = 0;

// --------------------- Global Flags ---------------------
static int destroy_flag = 0;
static int connection_flag = 0;
static int writeable_flag = 0;

// --------------------- Mutex ---------------------
pthread_mutex_t ma_mutex = PTHREAD_MUTEX_INITIALIZER;

// --------------------- Signal Handler ---------------------
static void INT_HANDLER(int signo) {
    destroy_flag = 1;
}

// --------------------- Utility Functions ---------------------

// Create a subdirectory "data/<instrument>".
void create_instrument_dir(const char *instr, char *dirpath, size_t size) {
    snprintf(dirpath, size, "data/%s", instr);
    mkdir("data", 0777);  // Ensure top-level "data" directory exists.
    if (mkdir(dirpath, 0777) == 0) {
        printf("[DEBUG] Created directory: %s\n", dirpath);
    }
}

// Get or create an instrument entry.
moving_avg_t* get_instrument(const char *instrument) {
    for (int i = 0; i < num_instruments; i++) {
        if (strcmp(instruments[i].instrument, instrument) == 0)
            return &instruments[i];
    }
    if (num_instruments < MAX_INSTRUMENTS) {
        moving_avg_t *inst = &instruments[num_instruments];
        strncpy(inst->instrument, instrument, sizeof(inst->instrument) - 1);
        inst->instrument[sizeof(inst->instrument) - 1] = '\0';
        inst->trade_count = 0;
        inst->ma_count = 0;
        inst->max_corr = -2.0;
        strcpy(inst->max_corr_symbol, "N/A");
        inst->max_corr_time = 0;

        char dirpath[128];
        create_instrument_dir(instrument, dirpath, sizeof(dirpath));
        char filename[256];

        // Open transactions file.
        snprintf(filename, sizeof(filename), "%s/transactions.csv", dirpath);
        inst->trans_file = fopen(filename, "w");
        if (inst->trans_file) {
            fprintf(inst->trans_file, "Timestamp,Price,Volume,ProcessingDelay\n");
            printf("[DEBUG] Opened transactions file: %s\n", filename);
        } else {
            printf("[ERROR] Could not open transactions file: %s\n", filename);
        }

        // Open moving average file.
        snprintf(filename, sizeof(filename), "%s/moving_average.csv", dirpath);
        inst->ma_file = fopen(filename, "w");
        if (inst->ma_file) {
            fprintf(inst->ma_file, "Timestamp,MovingAvg,TotalVolume,AvgProcessingDelay\n");
            printf("[DEBUG] Opened moving average file: %s\n", filename);
        } else {
            printf("[ERROR] Could not open moving average file: %s\n", filename);
        }

        // Open correlation file.
        snprintf(filename, sizeof(filename), "%s/correlation.csv", dirpath);
        inst->corr_file = fopen(filename, "w");
        if (inst->corr_file) {
            fprintf(inst->corr_file, "Timestamp,OtherSymbol,Correlation,MaxCorrMATime\n");
            printf("[DEBUG] Opened correlation file: %s\n", filename);
        } else {
            printf("[ERROR] Could not open correlation file: %s\n", filename);
        }

        num_instruments++;
        return inst;
    }
    fprintf(stderr, "Too many instruments!\n");
    return NULL;
}

// --------------------- Pearson Correlation Function ---------------------
// Compute Pearson correlation coefficient for two vectors of length n.
double pearson_corr_vector(const double *v1, const double *v2, int n) {
    if (n < 2)
        return NAN;
    double sum1 = 0, sum2 = 0;
    for (int i = 0; i < n; i++) {
        sum1 += v1[i];
        sum2 += v2[i];
    }
    double mean1 = sum1 / n;
    double mean2 = sum2 / n;
    double num = 0, den1 = 0, den2 = 0;
    for (int i = 0; i < n; i++) {
        double d1 = v1[i] - mean1;
        double d2 = v2[i] - mean2;
        num += d1 * d2;
        den1 += d1 * d1;
        den2 += d2 * d2;
    }
    if (den1 == 0 || den2 == 0)
        return NAN;
    return num / sqrt(den1 * den2);
}

// --------------------- Data Structures for Correlation Computation ---------------------
// This structure holds the most recent MA values and a mapping to the global instruments array.
typedef struct {
    int global_index;            // Index in the global instruments array.
    char instrument[16];
    ma_entry_t ma[MA_HISTORY_SIZE];
} corr_data_t;

// Thread argument for correlation computation.
typedef struct {
    int index;          // Index in the corr_data_t array for which to compute correlation.
    int total;          // Total number of instruments with complete MA history.
    corr_data_t *data;  // Array of correlation data.
    double current_time; // Current computation time.
} corr_thread_arg_t;

// Thread function to compute correlations for one instrument.
void *compute_corr_thread(void *arg) {
    corr_thread_arg_t *ct_arg = (corr_thread_arg_t *)arg;
    int idx = ct_arg->index;
    int total = ct_arg->total;
    double max_corr = -2.0;
    char max_sym[16] = "N/A";
    double max_ma_time = 0; // Timestamp of the MA value that maximizes the correlation
    int max_ma_index = -1;  // Index of the MA value that maximizes the correlation

    for (int j = 0; j < total; j++) {
        if (j == idx)
            continue;

        // Extract the moving average values for correlation computation
        double ma1[MA_HISTORY_SIZE], ma2[MA_HISTORY_SIZE];
        for (int k = 0; k < MA_HISTORY_SIZE; k++) {
            ma1[k] = ct_arg->data[idx].ma[k].moving_avg;
            ma2[k] = ct_arg->data[j].ma[k].moving_avg;
        }

        // Compute Pearson correlation
        double corr = pearson_corr_vector(ma1, ma2, MA_HISTORY_SIZE);

        // Update max correlation and corresponding timestamp
        if (!isnan(corr) && corr > max_corr) {
            max_corr = corr;
            strncpy(max_sym, ct_arg->data[j].instrument, sizeof(max_sym) - 1);
            max_sym[sizeof(max_sym) - 1] = '\0';

            // Compute the mean of the MA vectors
            double mean1 = 0, mean2 = 0;
            for (int k = 0; k < MA_HISTORY_SIZE; k++) {
                mean1 += ma1[k];
                mean2 += ma2[k];
            }
            mean1 /= MA_HISTORY_SIZE;
            mean2 /= MA_HISTORY_SIZE;

            // Find the MA value that maximizes the correlation
            double max_contrib = -1.0;
            for (int k = 0; k < MA_HISTORY_SIZE; k++) {
                double contrib = fabs((ma1[k] - mean1) * (ma2[k] - mean2));
                if (contrib > max_contrib) {
                    max_contrib = contrib;
                    max_ma_index = k;
                }
            }

            // Store the timestamp of the MA value that maximizes the correlation
            if (max_ma_index != -1) {
                max_ma_time = ct_arg->data[idx].ma[max_ma_index].timestamp;
            }
        }
    }

    // Update the corresponding global instrument using the stored global index
    int global_idx = ct_arg->data[idx].global_index;
    pthread_mutex_lock(&ma_mutex);

    strncpy(instruments[global_idx].max_corr_symbol, max_sym, sizeof(instruments[global_idx].max_corr_symbol) - 1);
    instruments[global_idx].max_corr_symbol[sizeof(instruments[global_idx].max_corr_symbol) - 1] = '\0';
    instruments[global_idx].max_corr = max_corr;
    instruments[global_idx].max_corr_time = ct_arg->current_time; // Timestamp when max correlation was computed
    instruments[global_idx].max_corr_ma_time = max_ma_time;      // Timestamp of the MA value that maximizes the correlation

    // Log the correlation result
    if (instruments[global_idx].corr_file) {
        char timestamp[20];
        time_t current_time = (time_t)ct_arg->current_time;
        struct tm *tm_info = localtime(&current_time);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

        char ma_timestamp[20];
        time_t ma_time = (time_t)max_ma_time;
        struct tm *ma_tm_info = localtime(&ma_time);
        strftime(ma_timestamp, sizeof(ma_timestamp), "%Y-%m-%d %H:%M:%S", ma_tm_info);

        fprintf(instruments[global_idx].corr_file, "%s,%s,%.4f,%s\n",
                timestamp, // Timestamp when max correlation was computed
                instruments[global_idx].max_corr_symbol,
                instruments[global_idx].max_corr,
                ma_timestamp); // Human-readable timestamp of the MA value
        fflush(instruments[global_idx].corr_file);
    }

    pthread_mutex_unlock(&ma_mutex);
    free(ct_arg);
    return NULL;
}

// --------------------- Trade Logging ---------------------
// Parse JSON trade data, use clock_gettime for high-resolution timing, and log each trade.
void save_trade(const char *json_str) {
    json_t *root, *data_array, *data_obj, *price_obj, *vol_obj, *instId_obj;
    json_error_t error;

    root = json_loads(json_str, 0, &error);
    if (!root) {
        fprintf(stderr, "JSON Parsing Error: %s\n", error.text);
        return;
    }
    data_array = json_object_get(root, "data");
    if (!json_is_array(data_array)) {
        json_decref(root);
        return;
    }
    size_t index;
    for (index = 0; index < json_array_size(data_array); index++) {
        data_obj = json_array_get(data_array, index);
        price_obj = json_object_get(data_obj, "last");
        // Use "vol" if available; otherwise, fallback to "lastSz".
        vol_obj = json_object_get(data_obj, "vol");
        if (!vol_obj)
            vol_obj = json_object_get(data_obj, "lastSz");
        instId_obj = json_object_get(data_obj, "instId");
        if (json_is_string(price_obj) && json_is_string(vol_obj) && json_is_string(instId_obj)) {
            double price = atof(json_string_value(price_obj));
            double vol = atof(json_string_value(vol_obj));
            const char *inst = json_string_value(instId_obj);

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            double now = ts.tv_sec + ts.tv_nsec / 1e9;

            pthread_mutex_lock(&ma_mutex);
            moving_avg_t *entry = get_instrument(inst);
            if (entry && entry->trade_count < TRADE_BUFFER_SIZE) {
                entry->trades[entry->trade_count].timestamp = now;
                entry->trades[entry->trade_count].price = price;
                entry->trades[entry->trade_count].volume = vol;
            
                // Compute processing delay.
                struct timespec ts2;
                clock_gettime(CLOCK_REALTIME, &ts2);
                double current = ts2.tv_sec + ts2.tv_nsec / 1e9;
                entry->trades[entry->trade_count].delay = current - now;

                entry->trade_count++;

                // Log the trade to the transactions file
                if (entry->trans_file) {
                    char timestamp[20];
                    time_t trade_time = (time_t)now;
                    struct tm *tm_info = localtime(&trade_time);
                    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

                    fprintf(entry->trans_file, "%s,%.2f,%.4f,%.9f\n",
                            timestamp, price, vol, entry->trades[entry->trade_count].delay);
                    fflush(entry->trans_file);
                }
            }
            pthread_mutex_unlock(&ma_mutex);
            printf(KYEL "[Transaction] %s - Price=%.2f, Vol=%.4f, Processing Delay=%.6f sec\n" RESET, inst, price, vol, entry->trades[entry->trade_count].delay);
        }
    }
    json_decref(root);
} 
// --------------------- 15-Minute Moving Average & Volume Computation ---------------------
// Compute average price, total volume, and average delay over trades in the last 15 minutes.
void compute_moving_avg_and_volume(moving_avg_t *entry, double now, ma_entry_t *ma_out) {
    double sum_price = 0, total_vol = 0, processing_delay_sum = 0;
    int count = 0;
    int new_trade_count = 0;
    trade_t temp[TRADE_BUFFER_SIZE];

    for (int i = 0; i < entry->trade_count; i++) {
        if (entry->trades[i].timestamp >= now - FIFTEEN_MINUTES) {
            sum_price += entry->trades[i].price;
            total_vol += entry->trades[i].volume;
            processing_delay_sum += entry->trades[i].delay;
            temp[new_trade_count++] = entry->trades[i];
            count++;

        }
    }

    memcpy(entry->trades, temp, new_trade_count * sizeof(trade_t));
    entry->trade_count = new_trade_count;

    if (count > 0) {
        ma_out->moving_avg = sum_price / count;
        ma_out->total_volume = total_vol;
        ma_out->avg_delay = processing_delay_sum / count;  // Average processing delay
    } else {
        ma_out->moving_avg = 0;
        ma_out->total_volume = 0;
        ma_out->avg_delay = 0;
    }
    ma_out->timestamp = now;
}

// --------------------- Per-Minute Worker Thread ---------------------
// Every minute, log the scheduled vs. actual start time difference, compute moving averages,
// update MA history for each instrument, and compute Pearson correlations.
void *per_minute_worker(void *arg) {
    (void)arg;
    while (!destroy_flag) {
        // Determine actual start time and the scheduled minute boundary.
        struct timespec ts_start;
        clock_gettime(CLOCK_REALTIME, &ts_start);
        double actual_start = ts_start.tv_sec + ts_start.tv_nsec / 1e9;
        double scheduled = floor(actual_start / 60.0) * 60.0;
        double time_diff = actual_start - scheduled;

        // Log timing difference.
        if (timing_file) {
            char ts_str[20];
            time_t t_int = ts_start.tv_sec;
            struct tm *tm_info = localtime(&t_int);
            strftime(ts_str, sizeof(ts_str), "%Y-%m-%d %H:%M:%S", tm_info);
            fprintf(timing_file, "%s,%.3f\n", ts_str, time_diff);
            fflush(timing_file);
        }
        printf(KBLU "[Timing] Scheduled vs Actual diff: %.3f sec\n" RESET, time_diff);


        // Calculate sleep duration until the next minute.
        double next_minute = ceil(actual_start / 60.0) * 60.0;
        double sleep_duration = next_minute - actual_start;

        // Sleep with nanosecond precision.
        struct timespec sleep_time;
        sleep_time.tv_sec = (time_t)sleep_duration;
        sleep_time.tv_nsec = (long)((sleep_duration - sleep_time.tv_sec) * 1e9);
        nanosleep(&sleep_time, NULL);


        // Compute moving averages.
        clock_gettime(CLOCK_REALTIME, &ts_start);
        double now = ts_start.tv_sec + ts_start.tv_nsec / 1e9;
        char timestamp[20];
        time_t now_int = ts_start.tv_sec;
        struct tm *tm_info = localtime(&now_int);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

        pthread_mutex_lock(&ma_mutex);
        for (int i = 0; i < num_instruments; i++) {
            ma_entry_t new_ma;
            compute_moving_avg_and_volume(&instruments[i], now, &new_ma);
            if (instruments[i].ma_count < MA_HISTORY_SIZE) {
                instruments[i].ma_history[instruments[i].ma_count] = new_ma;
                instruments[i].ma_count++;
            } else {
                for (int k = 1; k < MA_HISTORY_SIZE; k++) {
                    instruments[i].ma_history[k - 1] = instruments[i].ma_history[k];
                }
                instruments[i].ma_history[MA_HISTORY_SIZE - 1] = new_ma;
            }
            if (instruments[i].ma_file) {
                fprintf(instruments[i].ma_file, "%s,%.2f,%.4f,%.9f\n",
                        timestamp, new_ma.moving_avg, new_ma.total_volume, new_ma.avg_delay);
                fflush(instruments[i].ma_file);
            }
        }
        // Build correlation data array for instruments with complete MA history.
        int valid_count = 0;
        corr_data_t *corr_array = malloc(num_instruments * sizeof(corr_data_t));
        for (int i = 0; i < num_instruments; i++) {
            if (instruments[i].ma_count >= MA_HISTORY_SIZE) {
                corr_array[valid_count].global_index = i;
                strncpy(corr_array[valid_count].instrument, instruments[i].instrument,
                        sizeof(corr_array[valid_count].instrument) - 1);
                corr_array[valid_count].instrument[sizeof(corr_array[valid_count].instrument) - 1] = '\0';

                // Copy the MA history (including timestamps)
                memcpy(corr_array[valid_count].ma, instruments[i].ma_history, MA_HISTORY_SIZE * sizeof(ma_entry_t));
                valid_count++;
            }
        }
        pthread_mutex_unlock(&ma_mutex);

        // If there is more than one instrument with complete MA history, compute correlations.
        if (valid_count > 1) {
            pthread_t threads[valid_count];
            for (int i = 0; i < valid_count; i++) {
                corr_thread_arg_t *ct_arg = malloc(sizeof(corr_thread_arg_t));
                ct_arg->index = i;
                ct_arg->total = valid_count;
                ct_arg->data = corr_array;
                ct_arg->current_time = now;
                pthread_create(&threads[i], NULL, compute_corr_thread, ct_arg);
            }
            for (int i = 0; i < valid_count; i++) {
                pthread_join(threads[i], NULL);
            }
        }
        free(corr_array);
    }
    return NULL;
}

// --------------------- CPU Idle Monitor Thread ---------------------
// Reads /proc/stat every second, computes the CPU idle percentage, and logs it.
void *cpu_idle_monitor(void *arg) {
    (void)arg;
    FILE *fp;
    char buffer[256];
    unsigned long prev_idle = 0, prev_total = 0;

    cpu_idle_file = fopen("cpu_idle.csv", "w");
    if (cpu_idle_file) {
        fprintf(cpu_idle_file, "Timestamp,IdlePercent\n");
        fflush(cpu_idle_file);
    }

    while (!destroy_flag) {
        fp = fopen("/proc/stat", "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
                sscanf(buffer, "cpu  %lu %lu %lu %lu %lu %lu %lu %lu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
                unsigned long total = user + nice + system + idle + iowait + irq + softirq + steal;
                if (prev_total != 0) {
                    unsigned long d_total = total - prev_total;
                    unsigned long d_idle = idle - prev_idle;
                    double idle_percent = (d_total > 0) ? (100.0 * d_idle / d_total) : 0.0;

                    time_t now = time(NULL);
                    char ts[20];
                    struct tm *tm_info = localtime(&now);
                    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
                    if (cpu_idle_file) {
                        fprintf(cpu_idle_file, "%s,%.3f\n", ts, idle_percent);
                        fflush(cpu_idle_file);
                    }
                }
                prev_total = total;
                prev_idle = idle;
            }
            fclose(fp);
        }
        sleep(1);
    }
    if (cpu_idle_file)
        fclose(cpu_idle_file);
    return NULL;
}

// --------------------- WebSocket Write Helper ---------------------
static int websocket_write_back(struct lws *wsi_in, char *str, int str_size_in) {
    if (!str || !wsi_in)
        return -1;
    int len = (str_size_in < 1) ? strlen(str) : str_size_in;
    char *out = malloc(LWS_SEND_BUFFER_PRE_PADDING + len + LWS_SEND_BUFFER_POST_PADDING);
    if (!out)
        return -1;
    memcpy(out + LWS_SEND_BUFFER_PRE_PADDING, str, len);
    int n = lws_write(wsi_in, out + LWS_SEND_BUFFER_PRE_PADDING, len, LWS_WRITE_TEXT);
    printf(KBLU "[websocket_write_back] %s\n" RESET, str);
    free(out);
    return n;
}

// --------------------- WebSocket Callback ---------------------
static int ws_service_callback(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            printf(KYEL "[WebSocket] Connected to OKX\n" RESET);
            connection_flag = 1;
            // Subscribe to required symbols.
            websocket_write_back(wsi,
                "{\"op\":\"subscribe\",\"args\":["
                    "{\"channel\":\"tickers\",\"instId\":\"BTC-USDT\"},"
                    "{\"channel\":\"tickers\",\"instId\":\"ADA-USDT\"},"
                    "{\"channel\":\"tickers\",\"instId\":\"ETH-USDT\"},"
                    "{\"channel\":\"tickers\",\"instId\":\"DOGE-USDT\"},"
                    "{\"channel\":\"tickers\",\"instId\":\"XRP-USDT\"},"
                    "{\"channel\":\"tickers\",\"instId\":\"SOL-USDT\"},"
                    "{\"channel\":\"tickers\",\"instId\":\"LTC-USDT\"},"
                    "{\"channel\":\"tickers\",\"instId\":\"BNB-USDT\"}"
                "]}",
                -1
            );
            break;
        case LWS_CALLBACK_CLIENT_RECEIVE:
            printf(KCYN_L "[Price Update] %.*s\n" RESET, (int)len, (char *)in);
            save_trade((char *)in);
            break;
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            writeable_flag = 1;
            break;
        case LWS_CALLBACK_CLIENT_CLOSED:
            connection_flag = 0;
            printf(KRED "[WebSocket] Disconnected from OKX\n" RESET);
            break;
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            connection_flag = 0;
            printf(KRED "[WebSocket] Connection error.\n" RESET);
            break;
        default:
            break;
    }
    return 0;
}

// --------------------- WebSocket Protocol Definition ---------------------
static struct lws_protocols protocols[] = {
    {"example-protocol", ws_service_callback, 0, 1024},
    {NULL, NULL, 0, 0}
};

// --------------------- Main Function ---------------------
int main(void) {
    // Create top-level "data" directory.
    mkdir("data", 0777);

    // Open global timing log.
    timing_file = fopen("timing.csv", "w");
    if (timing_file) {
        fprintf(timing_file, "Timestamp,TimeDiff\n");
        fflush(timing_file);
    }

    // Set up signal handler.
    struct sigaction act;
    act.sa_handler = INT_HANDLER;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, 0);

    // Create WebSocket context.
    struct lws_context *context = NULL;
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    context = lws_create_context(&info);
    if (!context) {
        printf(KRED "[Main] Failed to create WebSocket context.\n" RESET);
        return -1;
    }
    printf(KGRN "[Main] WebSocket context created.\n" RESET);

    // Prepare client connection info.
    struct lws_client_connect_info clientInfo;
    memset(&clientInfo, 0, sizeof(clientInfo));
    clientInfo.context = context;
    clientInfo.address = "ws.okx.com";
    clientInfo.port = 8443;
    clientInfo.path = "/ws/v5/public";
    clientInfo.ssl_connection = LCCSCF_USE_SSL;
    clientInfo.host = "ws.okx.com";
    clientInfo.origin = "ws.okx.com";
    clientInfo.protocol = protocols[0].name;

    // Connect to OKX.
    struct lws *wsi = lws_client_connect_via_info(&clientInfo);
    if (!wsi) {
        printf(KRED "[Main] Failed to connect.\n" RESET);
    } else {
        printf(KGRN "[Main] WebSocket connected.\n" RESET);
    }

    // Create per-minute worker thread.
    pthread_t minute_thread;
    pthread_create(&minute_thread, NULL, per_minute_worker, NULL);

    // Create CPU idle monitor thread.
    pthread_t cpu_thread;
    pthread_create(&cpu_thread, NULL, cpu_idle_monitor, NULL);

    // Main loop: run WebSocket service and attempt reconnections if disconnected.
    time_t last_reconnect_attempt = 0;
    while (!destroy_flag) {
        lws_service(context, 50);
        if (!connection_flag) {
            time_t now = time(NULL);
            if (now - last_reconnect_attempt >= 10) { // Attempt reconnection every 10 seconds.
                printf(KRED "[Main] Attempting to reconnect...\n" RESET);
                wsi = lws_client_connect_via_info(&clientInfo);
                if (wsi) {
                    connection_flag = 1;
                    printf(KGRN "[Main] Reconnected.\n" RESET);
                } else {
                    printf(KRED "[Main] Reconnect attempt failed.\n" RESET);
                }
                last_reconnect_attempt = now;
            }
        }
    }

    printf("[Main] Closing connection...\n");
    lws_context_destroy(context);

    pthread_join(minute_thread, NULL);
    pthread_join(cpu_thread, NULL);

    // Close per-instrument files.
    for (int i = 0; i < num_instruments; i++) {
        if (instruments[i].trans_file)
            fclose(instruments[i].trans_file);
        if (instruments[i].ma_file)
            fclose(instruments[i].ma_file);
        if (instruments[i].corr_file)
            fclose(instruments[i].corr_file);
    }
    if (timing_file)
        fclose(timing_file);

    printf("[Main] WebSocket client terminated.\n");
    return 0;
}
