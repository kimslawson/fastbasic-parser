' Optimizations
dim arr(10) word
X = 10
poke 704 + 4, 15 * 16
graphics 8 + 16
graphics 0
for I = 0 to 10 step 1
  if X <> 0
    print I;
  endif
  if I > 5
    X = X - 1
  endif
  arr(I) = arr(I) + 1
next I
X = X + 1
pause 0
print peek(712) + abs(-X) + rand(10) * 0
print (X * 2) + (X / 2)
print "A"; "B"; chr$(67)
end
