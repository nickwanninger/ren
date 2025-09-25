(local dump (require :jit.dump))

(dump.on "m")

(local ffi (require :ffi))

(ffi.cdef (require :assets.flecs_cdef))

(print "Hello from Fennel!")


; (ffi.cdef "typedef struct { float x; float y; float z; float w; } vec4;")
; (ffi.cdef "typedef struct { float x; float y; float z; } vec3;")
; (ffi.cdef "typedef struct { float x; float y;} vec2;")

(fn partition [n xs opts]
  ; (assert (and (number? n) (> n 0)) "partition: n must be a positive integer")
  (let [out []
        len (# xs)
        drop? (and opts (. opts :drop?))]
    (for [i 1 len n]
      (let [stop (math.min len (+ i (- n 1)))
            chunk []]
        (for [j i stop 1]
          (table.insert chunk (. xs j)))
        (when (or (not drop?) (= (# chunk) n))
          (table.insert out chunk))))
    out))

(fn compile_struct_to_ffi [name fields]
  ;; Fields is [float x float y float z]
  (let [fielddefs (each [[type field] (partition 2 fields)]
                     (string.format "%s %s;" type field))]
    (string.format "typedef struct { %s } %s;"
                   (table.concat fielddefs " ")
                   name)))

(macro struct [name fields]
  ;; Fields is [float x float y float z]
  (let [compiled (compile_struct_to_ffi name fields)]
    (print compiled)
    `(ffi.cdef ,compiled)))


(struct vec3 [float x float y float z])




(print (string.dump compile_struct_to_ffi))


(fn _G.test []
  (print "Hello!"))