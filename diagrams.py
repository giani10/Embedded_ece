import matplotlib.pyplot as plt
import pandas as pd
import matplotlib.dates as mdates

# Load the data
cpu = pd.read_csv('pi_backup/cpu_idle.csv')

# Convert the Timestamp column to datetime
cpu['Timestamp'] = pd.to_datetime(cpu['Timestamp'])

# Option 1: Resample the data to 1-minute intervals (if data is very dense)
cpu_resampled = cpu.set_index('Timestamp').resample('10min').mean().reset_index()

# Option 2: Or use the full data but adjust the tick labels
plt.figure(figsize=(10, 5))

# Uncomment one of the following:
# Plot resampled data:
plt.plot(cpu_resampled['Timestamp'], cpu_resampled['IdlePercent'], marker='o', linestyle='-')

# Or plot full data:
# plt.plot(cpu['Timestamp'], cpu['IdlePercent'], marker='.', linestyle='-')

plt.xlabel('Time')
plt.ylabel('CPU Idle Percentage (%)', fontsize = 18)
plt.title('CPU Idle Percentage Over Time')

# Set major ticks to every hour
ax = plt.gca()
ax.xaxis.set_major_locator(mdates.HourLocator(interval=1))
ax.xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M'))

plt.xticks(rotation=45)
plt.tight_layout()
plt.savefig('cpu_idle.png')
plt.show()
