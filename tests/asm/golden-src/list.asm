; Walk a linked list and sum the node values.
; Watch D1 and D2 alternate, and the heat view trace the nodes.
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
node1:  db 5,  dw &node2
node2:  db 3,  dw &node3
node3:  db 11, dw &node4
node4:  db 23, dw 0
