((lambda (port)
   (write (cons 1 '(2 3)) port)
   (close-output-port port))
 (open-output-file "printed-list.txt"))
