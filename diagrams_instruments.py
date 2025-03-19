import matplotlib.pyplot as plt
import pandas as pd
import matplotlib.dates as mdates
import os

#List of instrumennts 
symbols = ["BTC-USDT", "ADA-USDT", "ETH-USDT", "DOGE-USDT", 
           "XRP-USDT", "SOL-USDT", "LTC-USDT", "BNB-USDT"]

# Create the directories for the plots 
os.makedirs("plots", exist_ok=True)

def generate_plots(symbol, file_type, resample_interval='10min'):
    try:
        df = pd.read_csv(f"data/{symbol}/{file_type}.csv")
        df['Timestamp'] = pd.to_datetime(df['Timestamp'])
    except FileNotFoundError:
        print(f"File not found for {symbol} - {file_type}")
        return

    plt.figure(figsize=(12, 6))
    
    if file_type == "transactions":
       y_column = "Price"
       title = f"{symbol} - Price Over Time"
       df_resampled = df.set_index('Timestamp').resample(resample_interval).mean().reset_index()

    elif file_type == "moving_average":
       y_column = "MovingAvg"
       title = f"{symbol} - 15-Minute Moving Average"
       df_resampled = df.set_index('Timestamp').resample(resample_interval).mean().reset_index()
    
    elif file_type == "correlation":
       y_column = "Correlation"
       title = f"{symbol} - Pearson Correlation"
       df_resampled = df[['Timestamp', y_column]].set_index('Timestamp').resample(resample_interval).mean().reset_index()
    
    else:
       return


    #Plotting
    plt.plot(df_resampled['Timestamp'], df_resampled[y_column], 
             marker='o', linestyle='-', markersize=3)
   
    #Axis configuration
    ax = plt.gca()
    ax.xaxis.set_major_locator(mdates.HourLocator(interval=2))
    ax.xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M'))
    
    plt.title(title)
    plt.xlabel("Time")
    plt.ylabel(y_column, fontsize = 18)
    plt.xticks(rotation=45)
    plt.tight_layout()
   
    #Save the plots
    os.makedirs(f"plots/{symbol}", exist_ok=True)
    plt.savefig(f"plots/{symbol}/{file_type}_plot.png")
    plt.close()

for symbol in symbols:
    generate_plots(symbol, "transactions")
    generate_plots(symbol, "moving_average")
    generate_plots(symbol, "correlation")

print("Plots created successfully /plots!")
