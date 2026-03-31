import numpy as np
import matplotlib.pyplot as plt

# Constants
B_min = 100  # Minimum byte threshold
s_values = [500, 1000, 2000]  # Different scaling factors for s
B_max = 10000  # Max B value for plotting

# B values (x-axis)
B = np.linspace(1, B_max, 500)

# Function to calculate B_eff
def calculate_Beff(B, s, B_min):
    B_eff = np.minimum(B, np.maximum(B_min, B * s / 1000))
    return B_eff

# Plotting
plt.figure(figsize=(8, 6))

for s in s_values:
    B_eff = calculate_Beff(B, s, B_min)
    plt.plot(B, B_eff, label=f"s = {s}")

# Labels and title
plt.title("Mapping of B_eff with different values of s", fontsize=14)
plt.xlabel("B (Real Byte Size)", fontsize=12)
plt.ylabel("B_eff (Effective Byte Size)", fontsize=12)
plt.legend(title="Scaling Factor (s)", loc="upper left")

# Show grid
plt.grid(True)

# Show plot
plt.show()