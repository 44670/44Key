import binascii
import hashlib
with open('bip39-words.txt','r') as f:
    bip39Words = f.read().splitlines()

wordToIndexMap = {}
for i in range(len(bip39Words)):
    wordToIndexMap[bip39Words[i].strip()] = i

print("Please enter your recovery seed, separated by spaces:")
seeds = input().strip().split(' ')
bits = []
for word in seeds:
    if word in wordToIndexMap:
        idx = wordToIndexMap[word]
        for i in range(0, 11):
            b = 0
            if idx & (1 << (10 - i)) != 0:
                b = 1
            bits.append(b)
    else:
        print("Invalid word: " + word)
        exit(1)

totalBits = len(bits)
checkSumBits = totalBits // (32 + 1)
keyBits = totalBits - checkSumBits
if keyBits % 32 != 0:
    print("Key bits must be a multiple of 32")
    exit(1)

key = b''
for i in range(0, keyBits//8):
    b = 0
    for j in range(0, 8):
        b |= bits[i*8 + j] << (7 - j)
    print(b)
    key += bytes([b])

print('Key: ', binascii.hexlify(key))

h = hashlib.sha256(key).digest()
for i in range(0, checkSumBits):
    if (h[i//8] & (1 << (7 - i%8)) == 0) != (bits[keyBits + i] == 0):
        print("WARNING: invalid checksum")
        break

