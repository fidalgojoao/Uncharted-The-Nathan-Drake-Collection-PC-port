"""reduce pw-flat.txt to non-noise mismatches: drop source-line deltas and string-tail noise"""
import re,sys
out=open('pw-real.txt','w')
n=0
for l in open('pw-flat.txt'):
    if 'PS4ONLY' in l:out.write(l);continue
    m=re.search(r'(value|short) ps3=([0-9a-f]{8})\S* ps4=([0-9a-f]{8})',l)
    if m:
        a=int(m.group(2),16);b=int(m.group(3),16)
        if a<0x10000 and b<0x10000:continue  # source line numbers
        if a>>16 and b>>16 and (a&0xffff)==(b>>16):continue  # line:count pairs swapped
        if (a>>16)==0 and (b>>16)==0:continue
        if (b>>16)==(a&0xffff) or (a>>16)==(b&0xffff):continue
        A=a.to_bytes(4,'big')
        if all(c==0 or 32<=c<127 for c in A) and 0 in A:continue
    if re.search(r'bytes ps3=b.u3final',l):continue
    out.write(l);n+=1
print(n)
