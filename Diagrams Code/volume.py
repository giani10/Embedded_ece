import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import os

# List of symbols and colors
symbols = ["BTC-USDT", "ADA-USDT", "ETH-USDT", "DOGE-USDT",
           "XRP-USDT", "SOL-USDT", "LTC-USDT", "BNB-USDT"]
colors = plt.cm.tab10.colors  # 8 distinct colors

plt.figure(figsize=(14, 8))

for idx, symbol in enumerate(symbols):
    try:
        # Load data with error checking
        file_path = f"data/{symbol}/transactions.csv"
        if not os.path.exists(file_path):
            print(f"File not found: {file_path}")
            continue
            
        df = pd.read_csv(file_path, 
                        parse_dates=['Timestamp'],
                        usecols=['Timestamp', 'Volume'])
        
        # Handle potential missing values
        if df['Volume'].isnull().any():
            print(f"Missing values found in {symbol}, filling with 0")
            df['Volume'] = df['Volume'].fillna(0)
        
        # Resample and sum with error handling
        try:
            df = df.set_index('Timestamp').resample('1H').sum().reset_index()
        except Exception as resample_error:
            print(f"Resampling error for {symbol}: {str(resample_error)}")
            continue

        # Plot with logarithmic scale
        plt.semilogy(df['Timestamp'], df['Volume'] + 1e-1,  # Add small epsilon to avoid log(0)
                   color=colors[idx],
                   linewidth=1.5,
                   alpha=0.8,
                   label=symbol)
        
    except Exception as e:
        print(f"Error processing {symbol}: {str(e)}")
        continue

# Configure plot aesthetics
plt.title('Trading Volume Comparison (Log Scale)', fontsize=14, pad=20)
plt.xlabel('Time', fontsize=12)
plt.ylabel('Volume (USDT)', fontsize=12)

# Set explicit axis limits and grid
plt.ylim(1e0, 1e6)  # 10^0 to 10^6
plt.yticks([1e0, 1e2, 1e4, 1e6, 1e8], 
          ['$10^0$', '$10^2$', '$10^4$', '$10^6$', '$10^8$'], 
          fontsize=10)
plt.grid(True, which='both', linestyle='--', alpha=0.4)

# Date formatting
ax = plt.gca()
ax.xaxis.set_major_locator(mdates.HourLocator(interval=4))
ax.xaxis.set_major_formatter(mdates.DateFormatter('%d/%m %H:%M'))
plt.xticks(rotation=45, ha='right')

# Legend configuration
plt.legend(bbox_to_anchor=(1.05, 1), 
          loc='upper left', 
          fontsize=10,
          frameon=True,
          framealpha=0.9)

# Save and display
plt.tight_layout()
plt.savefig('volume_comparison_log.png', dpi=300, bbox_inches='tight')
plt.show()
