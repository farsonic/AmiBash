classify() {
    case $1 in
        [0-9]|[1-9][0-9]) echo "$1: small number" ;;
        [0-9]*[!0-9]*) echo "$1: mixed" ;;
        [0-9]*) echo "$1: big number" ;;
        [a-z]*) echo "$1: lowercase word" ;;
        [A-Z]*) echo "$1: capitalised" ;;
        ""|" ") echo "empty or space" ;;
        *.txt|*.md) echo "$1: document" ;;
        -*) echo "$1: option" ;;
        *) echo "$1: other" ;;
    esac
}
for x in 5 42 123 12ab hello World "" " " a.txt README.md -v @; do classify "$x"; done
case "a*b" in a\*b) echo "escaped star";; esac
case "a*b" in "a*b") echo "quoted star";; esac
case abc in *c) echo "suffix";;& a*) echo "prefix too";; esac
case x in x) echo 1;& y) echo 2;& z) echo 3;; w) echo 4;; esac
shopt -s extglob
case foo.tar.gz in *.@(gz|bz2)) echo "compressed";; esac
case 007 in +([0-9])) echo "digits";; esac
case abc in !(x*)) echo "not x";; esac
shopt -u extglob
case [ in \[) echo "bracket";; esac
case '?' in \?) echo "question";; esac
case a in [[:alpha:]]) echo "alpha class";; esac
case 5 in [[:digit:]]) echo "digit class";; esac
case ' ' in [[:space:]]) echo "space class";; esac
case A in [[:lower:]]) echo lower;; [[:upper:]]) echo "upper class";; esac
case x in *) ;; esac; echo "status $?"
v=hello; case $v in "$v") echo "self match";; esac
case $(echo dyn) in dyn) echo "dynamic word";; esac
case a in b) echo b;; esac; echo "nomatch status $?"
