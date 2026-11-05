import re,collections
L=[l for l in open('ps4.dis',errors='replace')]
C=collections.defaultdict(list)
for i,l in enumerate(L):
  if 'call' in l and '[rax+0x1f8]' in l:
    for j in range(i+1,min(i+14,len(L))):
      m=re.search(r'call\s+QWORD PTR \[r[a-z0-9]+\+(0x[0-9a-f]+)\]',L[j])
      if m:
        ad=L[j].split(':')[0].strip();C[m.group(1)].append(ad);break
for k,v in sorted(C.items(),key=lambda x:-len(x[1])):print(k,len(v),v[:6])
