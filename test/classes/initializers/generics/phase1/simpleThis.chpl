record MyRecord {
  param x : int = 1;
  var y : int = 2;


  proc init(param xVal) {
    x = xVal;
    y = 1 + this.x;

    super.init();
  }
}

proc main() {
  var r : MyRecord(10) = new MyRecord(10);

  writeln(r);
}
