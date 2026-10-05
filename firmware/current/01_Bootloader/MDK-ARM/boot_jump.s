                PRESERVE8
                THUMB
                AREA    |.text|, CODE, READONLY
                EXPORT  Boot_JumpAsm
Boot_JumpAsm    PROC
                MSR     MSP, R0
                CPSIE   I
                BX      R1
                ENDP
                END
