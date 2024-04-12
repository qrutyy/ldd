import matplotlib.pyplot as plt

with open('testrand.txt', 'r') as file:
    text = file.read()

# Split the text by commas and convert each element to an integer
numbers = [int(num) for num in text.replace(" ", "").replace('\n', '').split(',') if len(num) != 0]

# Create a plot
plt.plot(numbers)
plt.xlabel('Index')
plt.ylabel('Number')
plt.title('Random Sequence Plot')

# Save the plot as a jpg file
plt.savefig('trand.jpg')

# Display the plot
plt.show()

