; The caller shadows site with false. The saved closure must still return the
; eighth record of this revision of mod_ad.sch from its defining environment.
((lambda (get-site)
   ((lambda (site) (get-site)) #f))
 ((lambda (site) (lambda () site))
  (car (cdr (cdr (cdr (cdr (cdr (cdr (cdr
    (read (open-input-file "ENV/mod_ad.sch"))))))))))))
