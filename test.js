print("test.js");
startScript("test2.js");
sleep(1.0);
startScript("test2.js");
print("~test.js");
print("received: " + JSON.stringify(receive()));
