with open('bip39-words.txt', 'r') as f:
    bip39Words = f.readlines()
print(len(bip39Words))

maxL = 0
ret = "const char* bip39Words[] = {"
for word in bip39Words:
    ret += '"%s",\n' % word.strip()
    maxL = max(maxL, len(word.strip()))
ret += '};'

print('maxLen', maxL)

with open('../Firmware/main/bip39.h', 'w') as f:

    f.write(ret)
