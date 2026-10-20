shopt -s nocasematch
case HELLO in hello) echo "case nocase";; *) echo no;; esac
[[ ABC == abc ]] && echo "[[ nocase"
[[ ABC == a* ]] && echo "[[ nocase glob"
[[ Foo =~ ^f ]] && echo "regex nocase"
shopt -u nocasematch
case HELLO in hello) echo "case nocase";; *) echo "case sensitive";; esac
[[ ABC == abc ]] || echo "[[ sensitive"
shopt nocasematch
shopt -q nocasematch || echo "q off"
shopt -s nocasematch; shopt -q nocasematch && echo "q on"
