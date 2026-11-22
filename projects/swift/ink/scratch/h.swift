import Ink
let s = ">p9%W=(rqCY/nw]%NY.BbQ+?\"4S#%|Zbhd=WY4OeDtK=<pTi\"^IyFAgvEEkaVk0zp[%XAhm?rZMG|5zH\"$K!7zk)vD8wE<95NnOS +iDdfi3g)HFNXz~?"
for n in [10,20,30,40,50,60,70,80,90,100,117] {
  let sub = String(s.prefix(n))
  print(n, terminator: " ")
  let t0 = ContinuousClock.now
  _ = MarkdownParser().parse(sub)
  print(ContinuousClock.now - t0)
}
