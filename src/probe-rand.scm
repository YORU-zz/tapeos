; probe-rand.scm -- boot script for the S-rand session:
; exercises the (random n) primitive and pokes the results
; into the evidence area for the host check (check_ts.py).
;   0xE690..0xE69F: 16 x (random 100)     (59024..)
;   0xE6A0..0xE6A7: 8 x (+ 1 (random 6))  (59040.., dice)
;   0xE6A8:         (random 1) == 0       (59048)
;   0xE6B0..:       marker "TSR1"         (59056..)
(define (fill i)
  (if (< i 16)
      (begin (poke (+ 59024 i) (random 100))
             (fill (+ i 1)))))
(fill 0)
(define (filld i)
  (if (< i 8)
      (begin (poke (+ 59040 i) (+ 1 (random 6)))
             (filld (+ i 1)))))
(filld 0)
(poke 59048 (random 1))
; (date)/(time) must fail safely (#f) while RTGET is a stub:
; poke 'T' (84) if a value came back, 'F' (70) for #f.
(poke 59049 (if (date) 84 70))
(poke 59050 (if (time) 84 70))
; (call addr [hl]): plant "ld hl,1234; or a; ret" at
; 0xE6C0 (59072) and "inc hl; or a; ret" at 0xE6C8
; (59080); poke 'T' (84) at 59077 / 59083 if the calls
; return the expected values.
(poke 59072 33)
(poke 59073 210)
(poke 59074 4)
(poke 59075 183)
(poke 59076 201)
(poke 59077 (if (= (call 59072) 1234) 84 70))
(poke 59080 35)
(poke 59081 183)
(poke 59082 201)
(poke 59083 (if (= (call 59080 41) 42) 84 70))
(poke 59056 84)
(poke 59057 83)
(poke 59058 82)
(poke 59059 49)
