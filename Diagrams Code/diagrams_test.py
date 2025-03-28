import glob
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import pandas as pd
from datetime import datetime

# List of symbols and colors
symbols = [
    "BTC-USDT", "ADA-USDT", "ETH-USDT", "DOGE-USDT",
    "XRP-USDT", "SOL-USDT", "LTC-USDT", "BNB-USDT"
]
colors = plt.cm.tab10.colors  # Use 8 colors from the Tab10 palette

# Create a dictionary to store data
symbol_data = {sym: {'time': [], 'value': []} for sym in symbols}

# Read data from CSV files
for symbol in symbols:
    try:
        # File path based on your folder structure
        df = pd.read_csv(f"data/{symbol}/moving_average.csv")
        df['Timestamp'] = pd.to_datetime(df['Timestamp'])  # Convert to datetime
        
        # Store data
        symbol_data[symbol]['time'] = df['Timestamp'].tolist()
        symbol_data[symbol]['value'] = df['MovingAvg'].tolist()
        
        print(f"Read {len(df)} data points for {symbol}")
        
    except FileNotFoundError:
        print(f"File not found for {symbol}")
    except Exception as e:
        print(f"Error processing {symbol}: {str(e)}")

# Create the plot
plt.figure(figsize=(14, 7))

for idx, (symbol, data) in enumerate(symbol_data.items()):
    if not data['time']:
        continue  # Skip symbols with no data
    
    # Convert to pandas Series for easier handling
    series = pd.Series(data['value'], index=pd.to_datetime(data['time'])).sort_index()
    
    # Calculate percentage change from the initial value
    base_value = series.iloc[0]
    rel_change = (series / base_value - 1) * 100  # Percentage change
    
    # Plot with unique color
    plt.plot(rel_change.index, rel_change.values, 
             label=symbol, color=colors[idx], linewidth=1.5)

# Configure the plot
plt.title('Percentage Change Comparison of Moving Averages', fontsize=14)
plt.xlabel('Time', fontsize=12)
plt.ylabel('Percentage Change (%)', fontsize=12)
plt.grid(True, linestyle='--', alpha=0.6)

# Format dates on the X-axis
ax = plt.gca()
ax.xaxis.set_major_locator(mdates.AutoDateLocator())
ax.xaxis.set_major_formatter(mdates.DateFormatter('%m/%d\n%H:%M'))  # Adjusted to MM/DD format
plt.xticks(rotation=45, ha='right')

# Place legend outside the plot
plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left', fontsize=10)

# Save and display
plt.tight_layout()
plt.savefig('moving_average_comparison.png', dpi=300, bbox_inches='tight')
plt.show()
