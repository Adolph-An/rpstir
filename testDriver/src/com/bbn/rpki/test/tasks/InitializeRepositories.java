/*
 * Created on Dec 12, 2011
 */
package com.bbn.rpki.test.tasks;

import com.bbn.rpki.test.objects.Util;

/**
 * <Enter the description of this type here>
 *
 * @author tomlinso
 */
public class InitializeRepositories extends Task {

  private final Model model;

  /**
   * @param model
   */
  public InitializeRepositories(Model model) {
    this.model = model;
  }

  /**
   * @see com.bbn.rpki.test.tasks.Task#run()
   */
  @Override
  public void run() {
    for (String serverName : model.getAllServerNames()) {
      String[] parts = serverName.split("/");
      String rsyncBase = model.getRsyncBase(serverName);
      Util.exec("Initialize Repository", false, null, null,
                null,
                "ssh",
                parts[0],
                "rm",
                "-rf",
                rsyncBase + "/*");
    }
  }

  /**
   * @see com.bbn.rpki.test.tasks.Task#getBreakdownCount()
   */
  @Override
  public int getBreakdownCount() {
    return 0;
  }

  /**
   * @see com.bbn.rpki.test.tasks.Task#getTaskBreakdown(int)
   */
  @Override
  public TaskBreakdown getTaskBreakdown(int n) {
    assert false;
    return null;
  }

  /**
   * @see com.bbn.rpki.test.tasks.Task#getLogDetail()
   */
  @Override
  protected String getLogDetail() {
    // TODO Auto-generated method stub
    return null;
  }

}