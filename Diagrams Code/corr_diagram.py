import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import seaborn as sns
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.collections import LineCollection
import os
from itertools import permutations

sns.set_theme(style="darkgrid")

# Configuration
DATA_DIR = 'data'
OUTPUT_DIR = 'correlation_plots'
PLOT_STYLE = {
    'figure_size': (18, 9),
    'line_width': 1.5,
    'title_fontsize': 16,
    'label_fontsize': 14,
    'tick_fontsize': 12,
    'grid_color': '#CCCCCC',
    'colorbar_label_size': 12,
    'colorbar_tick_size': 11
}

# Get list of all instruments
instruments = [d.split('-')[0] for d in os.listdir(DATA_DIR) 
              if os.path.isdir(os.path.join(DATA_DIR, d))]

# Create output directory if not exists
os.makedirs(OUTPUT_DIR, exist_ok=True)

def create_windows(data, window_size):
    return np.lib.stride_tricks.sliding_window_view(data, window_size)

def create_plot_directory(instrument):
    """Create instrument-specific plot directory"""
    path = os.path.join(OUTPUT_DIR, instrument)
    os.makedirs(path, exist_ok=True)
    return path

for pair in permutations(instruments, 2):
    instrument1, instrument2 = pair
    
    try:
        # Load data
        df1 = pd.read_csv(f'{DATA_DIR}/{instrument1}-USDT/moving_average.csv', 
                         parse_dates=['Timestamp'])
        df2 = pd.read_csv(f'{DATA_DIR}/{instrument2}-USDT/moving_average.csv', 
                         parse_dates=['Timestamp'])

        # Merge data
        merged = pd.merge_asof(df1.sort_values('Timestamp'),
                              df2.sort_values('Timestamp'), 
                              on='Timestamp',
                              suffixes=(f'_{instrument1}', f'_{instrument2}'))

        # Create windows
        windows1 = create_windows(merged[f'MovingAvg_{instrument1}'].values, 8)
        windows2 = create_windows(merged[f'MovingAvg_{instrument2}'].values, 8)

        # Calculate correlations
        correlation_values = []
        best_shifts = []
        timestamps = []

        for i in range(len(windows1) - 60):
            current_correlations = []
            for shift in range(61):
                if i - shift >= 0:
                    corr = np.corrcoef(windows1[i], windows2[i - shift])[0,1]
                    current_correlations.append(corr)
            
            if current_correlations:
                max_corr = max(current_correlations)
                best_shift = current_correlations.index(max_corr)
                correlation_values.append(max_corr)
                best_shifts.append(best_shift)
                timestamps.append(merged['Timestamp'][i + 7])

        # Visualization
        plt.style.use('seaborn-v0_8')
        fig, ax = plt.subplots(figsize=PLOT_STYLE['figure_size'])

        # Create color mapping
        cmap = LinearSegmentedColormap.from_list('time_shift', ['#2a9d8f', '#e9c46a'], N=60)
        norm = plt.Normalize(0, 60)

        # Create line segments with thinner lines
        points = np.array([mdates.date2num(timestamps), correlation_values]).T.reshape(-1, 1, 2)
        segments = np.concatenate([points[:-1], points[1:]], axis=1)

        lc = LineCollection(segments, cmap=cmap, norm=norm, 
                          linewidth=PLOT_STYLE['line_width'], alpha=0.8)
        lc.set_array(np.array(best_shifts))
        ax.add_collection(lc)

        # Configure plot
        ax.axhline(0, color='#264653', linestyle='--', alpha=0.7, linewidth=1)
        ax.set_xlim(min(timestamps), max(timestamps))
        ax.set_ylim(-1, 1)
        
        # Enhanced labels and titles
        ax.set_xlabel("Time", fontsize=PLOT_STYLE['label_fontsize'], labelpad=10)
        ax.set_ylabel("Pearson Correlation", fontsize=PLOT_STYLE['label_fontsize'], labelpad=10)
        ax.set_title(f"{instrument1}-{instrument2} Correlation with Optimal Time Lag\n(8-minute windows)", 
                    fontsize=PLOT_STYLE['title_fontsize'], pad=20, weight='bold')

        # Date formatting
        ax.xaxis.set_major_locator(mdates.HourLocator(interval=2))
        ax.xaxis.set_major_formatter(mdates.DateFormatter('%m/%d %H:%M'))
        plt.xticks(rotation=45, ha='right', fontsize=PLOT_STYLE['tick_fontsize'])
        plt.yticks(np.linspace(-1, 1, 5), fontsize=PLOT_STYLE['tick_fontsize'])

        # Colorbar settings
        cbar = plt.colorbar(lc, pad=0.01)
        cbar.set_label('Optimal Time Lag (minutes)', 
                      fontsize=PLOT_STYLE['colorbar_label_size'], labelpad=10)
        cbar.set_ticks([0, 30, 60])
        cbar.set_ticklabels(['Real-time (0)', '30 min', '60 min'])
        cbar.ax.tick_params(labelsize=PLOT_STYLE['colorbar_tick_size'])

        # Grid customization
        plt.grid(True, color=PLOT_STYLE['grid_color'], linestyle='--', linewidth=0.8)

        # Save to instrument-specific folder
        output_path = create_plot_directory(instrument1)
        plt.tight_layout()
        plt.savefig(f'{output_path}/{instrument1}_{instrument2}_correlation.png', 
                   dpi=300, bbox_inches='tight')
        plt.close()
        
    except Exception as e:
        print(f"Error processing {instrument1}-{instrument2}: {str(e)}")
        continue
