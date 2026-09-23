; Walk a linked list and sum the values. The default demo of the web
; version, as the first program of the standalone one.
        LD D1 <- [head]
loopA:  JZ done
        LD A <- [D1]
        ADD A <- [sum]
        LD [sum] <- A
        LD D2 <- [D1+1]
loopB:  JZ done
        LD A <- [D2]
        ADD A <- [sum]
        LD [sum] <- A
        LD D1 <- [D2+1]
        JMP loopA
done:   HLT
.ram
sum:    db 0
head:   dw &node1
node1:  db 5, dw &node2
node2:  db 3, dw &node3
node3:  db 11, dw 0
.data
banner: .file('banner.txt')
