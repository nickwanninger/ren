
(load "assets/scheme/loop.scm")
(load "assets/scheme/write.scm")


;; This defines a macro to create ECS components with given names and fields.
;; For example:
;; (define-component Position (f32 x) (f32 y))
;; ->
;; (ecs/run-script "struct Position {\nx = f32\ny = f32\n}")
;; (define Position (ecs/lookup "Position"))

;; Converts a list of fields in a component into a string representation in the FlecsScript DSL.
(define (defcomp/fields fields)
  (apply string-append
         (map (lambda (field)
                (string-append (symbol->string (cadr field))
                               " = "
                               (symbol->string (car field))
                               "\n"))
              fields)))



(define (string-prefix? prefix str)
  (and (>= (string-length str) (string-length prefix))
       (string=? (substring str 0 (string-length prefix)) prefix)))


(define (string-suffix? suffix str)
 (and (>= (string-length str) (string-length suffix))
      (string=? (substring str (- (string-length str)
                                  (string-length suffix)))
                suffix)))

;; take a symbol, and if it is <type> return type
;; otherwise return the symbol as a string
(define (symbol->flecs sym)
  (let ((s (symbol->string sym)))
    (if (and (string-prefix? "<" s)
             (string-suffix? ">" s))
        (substring s 1 (- (string-length s) 1))
        s)))
  
(define-macro (define-component name . fields)
  (let ((field-str (defcomp/fields fields))
        (type-name (symbol->flecs name)))
    `(begin
       (ecs/run-script (string-append "struct " ,type-name " {\n" ,field-str "}"))
       (define ,name (ecs/lookup ,type-name)))))



(define-component <position>
  (f32 x)
  (f32 y))


(define (println . args)
  (format #t "~a" (apply string-append (map (lambda (x) (format #f "~a" x)) args)))
  (newline))




(define (ecs/compile-struct args)
  (let ((name (car args))
        (fields (cdr args)))
    (let ((field-str (defcomp/fields fields)))
      (string-append "struct " (symbol->string name) " {\n" field-str))))


;; Compile a single scheme expression into a flecs ECS script expression (string)
(define (ecs/compile1 expr)
  (let ((cmd (car expr))
        (args (cdr expr)))
     (cond ((eq? cmd 'struct) (ecs/compile-struct args))
           (else "unknown"))))

(define (ecs/compile expr)
  (if (list? expr)
    (apply string-append (map ecs/compile1 expr))
    (ecs/compile1 expr)))