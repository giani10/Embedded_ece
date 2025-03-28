import matplotlib.pyplot as plt
import pandas as pd
import matplotlib.dates as mdates

# Load the timing data from CSV.
timing = pd.read_csv('pi_backup/timing.csv')

# Convert the "Timestamp" column to datetime objects.
timing['Timestamp'] = pd.to_datetime(timing['Timestamp'])

# Option 1: Downsample the data to reduce clutter.
# For example, resample to 5-minute intervals by averaging the TimeDiff values.
timing_resampled = timing.set_index('Timestamp').resample('5min').mean().reset_index()

# Create the plot.
plt.figure(figsize=(10, 5))
plt.plot(timing_resampled['Timestamp'], timing_resampled['TimeDiff'], marker='o', linestyle='-')
plt.xlabel('Time')
plt.ylabel('Time Difference (sec)', fontsize = 20)
plt.title('Scheduled vs. Actual Execution Time Difference')

# Set x-axis major ticks to every hour (or adjust as needed)
ax = plt.gca()
ax.xaxis.set_major_locator(mdates.HourLocator(interval=1))
ax.xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M'))

plt.xticks(rotation=45)
plt.tight_layout()
plt.savefig('time_diff.png')
plt.show()
