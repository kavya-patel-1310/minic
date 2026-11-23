total = 0
for i in range(3000):
    for j in range(3000):
        if (i * j) % 7 == 3:
            total += i - j
        else:
            total += 1
print(total)
