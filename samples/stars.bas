' Catch the falling stars - a sample program for fbp
'
' This is a readable source, try:
'   fbp -S samples/stars.bas    (minimized, for a 10-liner)
'   fbp -l samples/stars.lst    (back to a readable listing)

graphics 0
poke 752, 1          ' Hide the cursor
setcolor 2, 0, 0

dim starX(4), starY(4)
score = 0
lives = 3
playerX = 20

' Place all the stars at random positions
for i = 0 to 4
  starX(i) = rand(38) + 1
  starY(i) = rand(10)
next i

proc drawPlayer
  position playerX - 1, 22
  print " ^ ";
endproc

repeat
  ' Read the joystick
  joy = stick(0)
  if joy = 11 and playerX > 1 then playerX = playerX - 1
  if joy = 7 and playerX < 38 then playerX = playerX + 1
  exec drawPlayer

  for i = 0 to 4 step 1
    position starX(i), starY(i)
    print " ";
    starY(i) = starY(i) + 1
    if starY(i) = 22
      if abs(starX(i) - playerX) <= 1
        score = score + 10
        sound 0, 100, 10, 8
      else
        lives = lives - 1
        sound 0, 200, 12, 8
      endif
      starX(i) = rand(38) + 1
      starY(i) = 0
    endif
    position starX(i), starY(i)
    print "*";
  next i

  position 0, 0
  print "Score: "; score; "  Lives: "; lives; " ";
  pause 4
  sound
until lives = 0

position 15, 10
print "GAME"; chr$(32); "OVER"
end
