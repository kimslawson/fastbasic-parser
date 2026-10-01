' Several DATA and PROC definitions
data a() byte = 1,2,3
data b() byte = 4,5,6
proc show
  ? a(0), b(0)
endproc
for i=0 to 9 : if i > 5 then ? i
next i
data c() byte = 7,8,9
data big() byte = $12,$23,$45,
data       byte = $08,$09,$15
proc other x
  ? c(1) + x, big(4)
endproc
exec show : exec other 5
