fakeMain(p&Point):S {@"x = {p.x}, y = {p.y}"}

main(A) {
  P = Point(x=i(A[0]), y=A[1].i)
  Pnt = Point(&P)
  pr(P#Pnt)  //false
  prl(P~Pnt)  //true
  printPoint = fakeMain
  str=printPoint(&Pnt)
  prl(str)
}
